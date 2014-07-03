signature JSON =
sig
  type json = CInterface.vol
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

  val load_file : string -> parse_flags list -> json
  val dumps : json -> print_flags list -> string
end

structure Json : JSON =
struct
  structure C = CInterface

  type json = C.vol
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
end

(* vim: se ai et: *)
