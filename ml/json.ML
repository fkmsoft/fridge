signature JSON =
sig
  type t = CInterface.vol
  type iter = CInterface.vol
  type error = {
    line : int,
    column : int,
    position : int,
    source : string,
    text : string }

  exception Parse_Error of error
  exception Invalid_Indent of int
  exception Key_Error of string

  datatype parse_flags =
    Reject_Duplicates |
    Disable_Eof_Check |
    Decode_Any |
    Decode_Int_As_Real

  datatype print_flags =
    Indent of int |
    Compact |
    Ensure_Ascii |
    Sort_Keys |
    Preserve_Order |
    Encode_Any |
    Escape_Slash

  (** functions **)

  (* loading / utility *)
  val load_file : string -> parse_flags list -> t
  val dumps : t -> print_flags list -> string
  val decref : t -> unit

  (* objects *)
  val object_size : t -> int
  val object_find : t -> string -> t option
  val object_get : t -> string -> t
  val object_set_new : t -> string -> t -> unit

  val object_iter : t -> iter
  val object_key_to_iter : string -> iter
  val object_iter_next : t -> iter -> iter
  val object_iter_key : iter -> string
  val object_iter_value : iter -> t
  val object_foreach : t -> (string -> t -> int -> 'a) -> 'a list

  (* arrays *)
  val array_size : t -> int
  val array_get : t -> int -> t
  val array_foreach : t -> (t -> int -> 'a) -> 'a vector

  (* primitives *)
  val integer : int -> t
  val integer_value : t -> int
  val string_value : t -> string
  val real_value : t -> real
end

structure Json : JSON =
struct
  structure C = CInterface

  type t = C.vol
  type iter = C.vol
  type error = {
    line : int,
    column : int,
    position : int,
    source : string,
    text : string }

  exception Parse_Error of error
  exception Invalid_Indent of int
  exception Key_Error of string

  datatype parse_flags =
    Reject_Duplicates |
    Disable_Eof_Check |
    Decode_Any |
    Decode_Int_As_Real
  
  datatype print_flags =
    Indent of int |
    Compact |
    Ensure_Ascii |
    Sort_Keys |
    Preserve_Order |
    Encode_Any |
    Escape_Slash
  
  fun int_of_parse_flag Reject_Duplicates = 0x1
    | int_of_parse_flag Disable_Eof_Check = 0x2
    | int_of_parse_flag Decode_Any = 0x4
    | int_of_parse_flag Decode_Int_As_Real = 0x8

  fun int_of_print_flag (Indent i) =
      if i > (0x20 - 1) then raise (Invalid_Indent i)
      else i
    | int_of_print_flag Compact = 0x20
    | int_of_print_flag Ensure_Ascii = 0x40
    | int_of_print_flag Sort_Keys = 0x80
    | int_of_print_flag Preserve_Order = 0x100
    | int_of_print_flag Encode_Any = 0x200
    | int_of_print_flag Escape_Slash = 0x400

  val gets = C.get_sym "/usr/lib/libjansson.so"

  val source_size = 160
  val text_size = 80
  val int_fields = 3 * (C.sizeof C.Cint)

  fun error_from_c e =
    { line = C.fromCint (C.offset 0 C.Cint e),
      column = C.fromCint (C.offset 1 C.Cint e),
      position = C.fromCint (C.offset 2 C.Cint e),
      source = C.fromCstring (C.address (C.offset int_fields C.Cchar e)),
      text = C.fromCstring (C.address (C.offset (int_fields + text_size) C.Cchar e)) }

  fun alloc_error () =
    C.alloc (int_fields + source_size + text_size) C.Cchar

  fun load_file file flags =
    let
      val lower_load =
        C.call3 (gets "json_load_file") (C.STRING, C.INT, C.POINTER) C.POINTER
      fun or_flags [] x = x
        | or_flags (f::fs) x = or_flags fs (x + (int_of_parse_flag f))
      val err = alloc_error ()
      val obj = lower_load (file, (or_flags flags 0), C.address err)
    in
      if C.fromCint obj = 0 then
        raise Parse_Error (error_from_c err)
      else
        obj
    end
  
  fun dumps obj flags =
    let
      val low_dumps = C.call2 (gets "json_dumps") (C.POINTER, C.INT) C.STRING
      fun or_flags [] x = x
        | or_flags (f::fs) x = or_flags fs (x + (int_of_print_flag f))
    in
      low_dumps (obj, or_flags flags 0)
    end

  fun decref obj =
    (C.call1 (gets "json_decref") C.POINTER C.VOID) obj

  fun object_size obj =
    (C.call1 (gets "json_object_size") C.POINTER C.INT) obj

  fun object_get obj key =
    let
      val ret = (C.call2 (gets "json_object_get") (C.POINTER, C.STRING) C.POINTER)
        (obj, key)
    in
      if C.fromCint ret = 0 then
        raise Key_Error key
      else
        ret
    end

  fun object_find obj key =
    SOME (object_get obj key) handle Key_Error s => NONE

  fun object_set_new obj k v =
    (C.call3 (gets "json_object_set_new") (C.POINTER, C.STRING, C.POINTER) C.VOID)
      (obj, k, v)

  fun object_iter obj =
    (C.call1 (gets "json_object_iter") C.POINTER C.POINTER) obj

  fun object_key_to_iter key =
    (C.call1 (gets "json_object_key_to_iter") C.STRING C.POINTER) key

  fun object_iter_next obj iter =
    (C.call2 (gets "json_object_iter_next") (C.POINTER, C.POINTER) C.POINTER)
      (obj, iter)

  fun object_iter_key iter =
    (C.call1 (gets "json_object_iter_key") C.POINTER C.STRING) iter

  fun object_iter_value iter =
    (C.call1 (gets "json_object_iter_value") C.POINTER C.POINTER) iter

  fun object_foreach obj f =
    let
      fun iter key i =
        if key <> "" then
          (f key (object_iter_value (object_key_to_iter key)) i) ::
            (iter
              (object_iter_key (object_iter_next obj (object_key_to_iter key)))
              (i + 1))
        else
          []
    in
      iter (object_iter_key (object_iter obj)) 0
    end

  fun array_size a =
    (C.call1 (gets "json_array_size") C.POINTER C.INT) a

  fun array_get a i =
    (C.call2 (gets "json_array_get") (C.POINTER, C.INT) C.POINTER) (a, i)

  fun array_foreach a f =
    Vector.tabulate (array_size a, fn i => f (array_get a i) i)

  fun integer i =
    (C.call1 (gets "json_integer") C.INT C.POINTER) i

  fun integer_value i =
    (C.call1 (gets "json_integer_value") C.POINTER C.INT) i

  fun string_value s =
    (C.call1 (gets "json_string_value") C.POINTER C.STRING) s

  fun real_value r =
    (C.call1 (gets "json_real_value") C.POINTER C.DOUBLE) r
end

(* vim: se ai et: *)
