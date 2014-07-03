fun string_of_json file =
  Json.dumps (Json.load_file file []) [Json.Sort_Keys]
  handle Json.Parse_Error {source, text, ...} =>
      "Error parsing " ^ source ^ ": " ^ text

fun main () =
  let
    val files = case (CommandLine.arguments ()) of
      [] => ["../conf/ghost.json"] |
      a => a
    val strs = map string_of_json files
    fun do_print [] = ()
      | do_print (s::r) = (print (s ^ "\n"); do_print r)
  in
    do_print strs
  end

(* vim: se ai et: *)
