structure C = CInterface

signature JSON =
sig
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

  val load_file : string -> parse_flags list -> json
  val dumps : json -> print_flags list -> string
end

structure Json : JSON =
struct
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
        | or_flags (Reject_Duplicates::fs) x = or_flags fs (x + 1)
        | or_flags (Disable_Eof_Check::fs) x = or_flags fs (x + 2)
        | or_flags (Decode_Any::fs) x = or_flags fs (x + 4)
        | or_flags (Decode_Int_As_Real::fs) x = or_flags fs (x + 8)
      val obj = lower_load (file, (or_flags flags 0), C.address err)
      val msg = C.fromCstring (C.address (C.offset (int_fields + text_size) C.Cchar err))
      val error = {
        line = C.fromCint err,
        column = C.fromCint (C.offset 1 C.Cint err),
        position = C.fromCint (C.offset 3 C.Cint err),
        source = C.fromCstring (C.address (C.offset int_fields C.Cchar err)),
        text = msg
      }
    in
      if size msg > 0 then
        raise (Parse_Error error)
      else
        obj
    end
  
  fun dumps obj flags =
    let
      val low_dumps = C.call2 (gets "json_dumps") (C.POINTER, C.INT) C.STRING
      fun or_flags [] x = x
        | or_flags (Indent i::fs) x = or_flags fs (x + i)
        | or_flags (Compact::fs) x = or_flags fs (x + 0x20)
        | or_flags (Ensure_Ascii::fs) x = or_flags fs (x + 0x40)
        | or_flags (Sort_Keys::fs) x = or_flags fs (x + 0x80)
        | or_flags (Preserve_Order::fs) x = or_flags fs (x + 0x100)
        | or_flags (Encode_Any::fs) x = or_flags fs (x + 0x200)
        | or_flags (Escape_Slash::fs) x = or_flags fs (x + 0x400)
    in
      low_dumps (obj, or_flags flags 0)
    end
end

(* vim: se ai et: *)
