signature ENGINE =
sig
  datatype dir = Left | Right
  datatype hit = Hit_None | Hit_Top | Hit_Left | Hit_Right | Hit_Bot
  datatype state = Idle | Walk | Fall | Jump | Hang

  type renderer = CInterface.vol
  type texture = CInterface.vol
  type font = CInterface.vol

  type point = { x : int, y : int }
  type line = { p : int, a : int, b : int }
  type rect = { x : int, y : int, w : int, h : int }
  type level = {
    background : texture,
    dim : rect,
    vertical : line list,
    horizontal : line list }

  type animation_rule = {
    frames : int list,
    duration : int list,
    box : rect }

  type entity_rule = {
    start_dim : rect,
    walk_dist : int,
    jump_dist : int * int,
    jump_time : int,
    fall_dist : int,
    has_gravity : bool,
    anim : animation_rule vector }

  type animation_state = {
    pos : int,
    frame : int,
    remaining : int }

  datatype jump_type = Jump_Wide | Jump_High | Jump_Hang

  type entity_state = {
    active : bool,
    pos : point,
    hitbox : rect,
    spawn : rect,
    dir : dir,
    st : state,
    jump_timeout : int,
    fall_time : int,
    anim : animation_state,
    rule : entity_rule,
    tex : texture }

  type entity_event = {
    walk : bool,
    move_left : bool,
    move_right : bool,
    move_jump : bool }

  type move_log = {
    walked : int,
    jumped : int,
    fallen : int,
    turned : bool,
    hang : bool }

  type debug_state = {
    active : bool,
    frames : bool,
    hitboxes : bool,
    pause : bool,
    show_terrain_collision : bool,
    terrain_collision : texture,
    message_positions : bool,
    font : font }

  (*** loading ***)
  val load_anim : Json.json -> string -> string -> animation_rule
  val load_asset_tex : Json.json -> string -> renderer -> string -> texture
  val load_entity_rule : Json.json -> string -> entity_rule
  val load_entities : string -> string -> renderer -> (texture list * entity_rule list * Json.json)
  val load_entity_resource : Json.json -> string -> texture -> renderer -> string -> entity_rule
  val load_state : entity_state -> entity_state
  val init_entity_state : entity_state -> entity_rule -> texture -> animation_state -> entity_state
  val clear_debug : debug_state -> debug_state

  (*** teardown ***)
  val destroy_level : level -> unit

  (*** state updates ***)
  val clear_order : entity_event -> entity_event
  val tick_animation : entity_state -> entity_state
  val kick_entity : entity_state -> hit -> point -> entity_state

  (*** movement ***)
  val keystate_to_movement : char vector -> entity_event
  val move_entity : entity_state -> entity_event -> level -> move_log * entity_state

  (*** collision ***)
  val collides_with_terrain : rect -> level -> hit
  val stands_on_terrain : rect -> level -> bool
  val entity_hitbox : entity_state -> rect
  val cmp_lines : unit -> unit
  val entity_feet : rect -> point
  
  (*** rendering ***)
  val draw_background : renderer -> texture -> rect -> unit
  val draw_terrain_lines : renderer -> level -> rect -> unit
  val render_line : renderer -> string -> font -> int -> unit
  val draw_entity : renderer -> rect -> entity_state -> debug_state -> unit

  (*** Json utility ***)
  val get_asset : Json.json -> string -> string
  val get_int_field : Json.json -> string -> string -> int
  val get_float_field : Json.json -> string -> string -> real

  (*** SDL utility ***)
  val load_texture : renderer -> string -> texture

  (*** general utility ***)
  val set_path : unit -> unit
end

(*structure Engine : ENGINE =*)
structure Engine =
struct
  datatype dir = Left | Right
  datatype hit = Hit_None | Hit_Top | Hit_Left | Hit_Right | Hit_Bot
  datatype state = Idle | Walk | Fall | Jump | Hang

  type renderer = CInterface.vol
  type texture = CInterface.vol
  type font = CInterface.vol

  type point = { x : int, y : int }
  type line = { p : int, a : int, b : int }
  type rect = { x : int, y : int, w : int, h : int }
  type level = {
    background : texture,
    dim : rect,
    vertical : line list,
    horizontal : line list }

  type animation_rule = {
    frames : int list,
    duration : int list,
    box : rect }

  type entity_rule = {
    start_dim : rect,
    walk_dist : int,
    jump_dist : int * int,
    jump_time : int,
    fall_dist : int,
    has_gravity : bool,
    anim : animation_rule vector }

  type animation_state = {
    pos : int,
    frame : int,
    remaining : int }

  datatype jump_type = Jump_Wide | Jump_High | Jump_Hang

  type entity_state = {
    active : bool,
    pos : point,
    hitbox : rect,
    spawn : rect,
    dir : dir,
    st : state,
    jump_timeout : int,
    fall_time : int,
    anim : animation_state,
    rule : entity_rule,
    tex : texture }

  type entity_event = {
    walk : bool,
    move_left : bool,
    move_right : bool,
    move_jump : bool }

  type move_log = {
    walked : int,
    jumped : int,
    fallen : int,
    turned : bool,
    hang : bool }

  type debug_state = {
    active : bool,
    frames : bool,
    hitboxes : bool,
    pause : bool,
    show_terrain_collision : bool,
    terrain_collision : texture,
    message_positions : bool,
    font : font }

  (*** loading ***)
  exception Load_Err

  fun force option =
    case option of
      SOME v => v |
      NONE => raise Load_Err

  fun load_anim src name key =
    let
      val obj = force (Json.object_get src key)
      val frames = force (Json.object_get obj "frames")
      val dur = force (Json.object_get obj "duration")
      fun load_lists i frs durs =
        if i >= 0 then
          let
            val frame = Json.integer_value (Json.array_get frames i)
            val duration = Json.integer_value (Json.array_get dur i)
          in
            load_lists (i - 1) (frame::frs) (duration::durs)
          end
        else
          (frs, durs)
      fun load_rect r =
        { x = Json.integer_value (Json.array_get r 0),
          y = Json.integer_value (Json.array_get r 1),
          w = Json.integer_value (Json.array_get r 2),
          h = Json.integer_value (Json.array_get r 3) }
      fun finish_load () =
        let
          val k = Json.array_size frames
          val (frs, durs) = load_lists (k - 1) [] []
          val box = load_rect (force (Json.object_get obj "box"))
        in
          { frames = frs, duration = durs, box = box }
        end
    in
      if (Json.array_size frames) <> (Json.array_size dur) then
        raise Load_Err
      else
        finish_load ()
    end
end

(* vim: se ai et: *)
