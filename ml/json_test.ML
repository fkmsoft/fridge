fun main () =
  let
    val file = "../conf/ghost.json"
    val msg =  Json.dumps (Json.load_file file []) [Json.Sort_Keys] handle
      Json.Parse_Error {source=src, text=err, ...} => "Error parsing " ^ src ^ ": " ^ err
  in
    print (msg ^ "\n")
  end

(* vim: se ai et: *)
