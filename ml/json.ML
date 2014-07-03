structure Json =
struct
val gets = CInterface.get_sym "/usr/lib/libjansson.so";

val load_file = CInterface.call3 (gets "json_load_file") (CInterface.STRING, CInterface.INT, CInterface.INT) CInterface.POINTER;

val dumps = CInterface.call2 (gets "json_dumps") (CInterface.POINTER, CInterface.INT) CInterface.STRING;
end;
