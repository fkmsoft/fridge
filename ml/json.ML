signature JSON =
sig
  type json = CInterface.vol
  type iter = CInterface.vol
  type error = {
    line : int,
    column : int,
    position : int,
    source : string,
    text : string }

  exception Parse_Error of error
  exception Invalid_Indent of int

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
  val load_file : string -> parse_flags list -> json
  val dumps : json -> print_flags list -> string
  val decref : json -> unit

  (* objects *)
  val object_size : json -> int
  val object_get : json -> string -> json option
  val object_set_new : json -> string -> json -> unit

  val object_iter : json -> iter
  val object_key_to_iter : string -> iter
  val object_iter_next : json -> iter -> iter
  val object_iter_key : iter -> string
  val object_iter_value : iter -> json
  val object_foreach : json -> (string -> json -> int -> 'a) -> 'a list

  (* arrays *)
  val array_size : json -> int
  val array_get : json -> int -> json

  (* primitives *)
  val integer : int -> json
  val integer_value : json -> int
  val string_value : json -> string
  val real_value : json -> real
end

structure Json : JSON =
struct
  structure C = CInterface

  type json = C.vol
  type iter = C.vol
  type error = {
    line : int,
    column : int,
    position : int,
    source : string,
    text : string }

  exception Parse_Error of error

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
  
  exception Invalid_Indent of int

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

  fun load_file file flags =
    let
      val lower_load =
        C.call3 (gets "json_load_file") (C.STRING, C.INT, C.POINTER) C.POINTER
      val int_fields = 3 * (C.sizeof C.Cint)
      val source_size = 160
      val text_size = 80
      val err = C.alloc (int_fields + source_size + text_size) C.Cchar
      fun or_flags [] x = x
        | or_flags (f::fs) x = or_flags fs (x + (int_of_parse_flag f))
      val obj = lower_load (file, (or_flags flags 0), C.address err)
      val error = {
        line = C.fromCint (C.offset 0 C.Cint err),
        column = C.fromCint (C.offset 1 C.Cint err),
        position = C.fromCint (C.offset 2 C.Cint err),
        source = C.fromCstring (C.address (C.offset int_fields C.Cchar err)),
        text = C.fromCstring (C.address (C.offset (int_fields + text_size) C.Cchar err))
      }
    in
      if size (#text error) > 0 then
        raise (Parse_Error error)
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
      val low_get = C.call2 (gets "json_object_get") (C.POINTER, C.STRING) C.POINTER
      val ret = low_get (obj, key)
    in
      if C.fromCint ret <> 0 then
        SOME ret
      else
        NONE
    end

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
