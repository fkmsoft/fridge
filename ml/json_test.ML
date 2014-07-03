fun main () =
let val obj = Json.load_file ("../conf/ghost.json", 0, 0)
 in print (Json.dumps (obj, 0))
end
