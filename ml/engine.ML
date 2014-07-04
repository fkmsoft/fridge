signature ENGINE =
sig
  datatype dir = Left | Right
  datatype hit = Hit_None | Hit_Top | Hit_Left | Hit_Right | Hit_Bot
  datatype state = Idle | Walk | Fall | Jump | Hang

  type point = { x : int, y : int }
  type line = { p : int, a : int, b : int }
  type rect = { x : int, y : int, w : int, h : int }
  type level = {
    background : Sdl.texture,
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
    jump_factor : real * real,
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
    jump_type : jump_type,
    fall_time : int,
    anim : animation_state,
    rule : entity_rule,
    tex : Sdl.texture }

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
    terrain_collision : Sdl.texture,
    message_positions : bool,
    font : Sdl.font }

  (*** magic constants ***)
  val asset_dir : string
  val conf_dir : string
  val state_names : string list

  (*** loading ***)
  val load_anim : Json.json -> string -> string -> animation_rule
  val load_asset_tex : Json.json -> string -> Sdl.renderer -> string -> Sdl.texture
  val load_entity_rule : Json.json -> string -> entity_rule
  val load_entities : string -> string -> Sdl.renderer -> (Sdl.texture * entity_rule) list
  val load_entity_resource : Json.json -> string -> Sdl.renderer -> string -> Sdl.texture * entity_rule
  val load_state : entity_state -> entity_state
  val init_entity_state : entity_state -> entity_rule option -> Sdl.texture option -> state -> entity_state
  val clear_debug : debug_state -> debug_state

  (*** teardown ***)
  val destroy_level : level -> unit

(*
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
  val draw_background : Sdl.renderer -> Sdl.texture -> rect -> unit
  val draw_terrain_lines : Sdl.renderer -> level -> rect -> unit
  val render_line : Sdl.renderer -> string -> Sdl.font -> int -> unit
  val draw_entity : Sdl.renderer -> rect -> entity_state -> debug_state -> unit
  *)

  (*** Json utility ***)
  val get_asset : Json.json -> string -> string
  val get_int_field : Json.json -> string -> string -> int
  val get_float_field : Json.json -> string -> string -> real

  (*** SDL utility ***)
  val load_texture : Sdl.renderer -> string -> Sdl.texture

  (*** general utility ***)
  val set_path : unit -> unit
end

structure Engine : ENGINE =
struct
  datatype dir = Left | Right
  datatype hit = Hit_None | Hit_Top | Hit_Left | Hit_Right | Hit_Bot
  datatype state = Idle | Walk | Fall | Jump | Hang

  type point = { x : int, y : int }
  type line = { p : int, a : int, b : int }
  type rect = { x : int, y : int, w : int, h : int }
  type level = {
    background : Sdl.texture,
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
    jump_factor : real * real,
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
    jump_type : jump_type,
    fall_time : int,
    anim : animation_state,
    rule : entity_rule,
    tex : Sdl.texture }

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
    terrain_collision : Sdl.texture,
    message_positions : bool,
    font : Sdl.font }

  (*** magic constants ***)
  val asset_dir = "assets"
  val conf_dir = "conf"
  val state_names = ["idle", "walk", "fall", "jump", "hang"]

  (*** Json utility ***)
  exception Load_Err

  fun force option =
    case option of
      SOME v => v |
      NONE => raise Load_Err

  fun get_asset a k =
    Json.string_value (force (Json.object_get a k))

  exception Custom

  fun get_int_field obj n s =
    case Json.object_get obj s of
      SOME v => Json.integer_value v |
      NONE =>
        if s = "custom-rule" then
          raise Custom
        else
          raise Load_Err

  fun get_float_field obj n s =
    case Json.object_get obj s of
      SOME v => Json.real_value v |
      NONE =>
        if s = "custom-rule" then
          raise Custom
        else
          raise Load_Err

  (*** SDL utility ***)
  fun load_texture r file =
    let
      val tmp = Sdl.img_load file
      val tex = Sdl.create_texture_from_surface r tmp
    in
      Sdl.free_surface tmp;
      tex
    end

  (*** general utility ***)
  fun set_path () = ()

  (*** loading ***)
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

  fun load_asset_tex a root r k =
    let
      val f = get_asset a k
      val p = root ^ "/" ^ "/" ^ asset_dir ^ "/" ^ f
    in
      load_texture r p
    end

  fun load_entity_rule src n =
    let
      val i = get_int_field src n
      val f = get_float_field src n
    in {
      walk_dist = i "walk-dist",
      jump_dist = (i "jump-dist-x", i "jump-dist-y"),
      jump_time = i "jump-time",
      fall_dist = i "fall-dist",
      jump_factor = (f "wide-jump-factor", f "high-jump-factor"),
      has_gravity = case Json.object_get src "has-gravity" of
        SOME s => Json.string_value s = "yes" |
        NONE => true,
      anim = Vector.fromList [],
      start_dim = { x = 0, y = 0, w = 0, h = 0 }}
     end

  fun load_entity_resource src n r root =
    let
      val obj = force (Json.object_get src "resource")
      val ps = Json.string_value obj
      val path = root ^ "/" ^ conf_dir ^ "/" ^ ps
      val er = load_entity_rule src n
      val res = Json.load_file path []
      val t = load_asset_tex res root r "asset"
      val siz = force (Json.object_get res "frame_size")
      fun i x = Json.integer_value (Json.array_get siz x)
      val start_dim = { x = 0, y = 0, w = i 0, h = i 1 }
      val anims = map (fn s => load_anim res n s) state_names
    in
      Json.decref res;
      (t, {
        start_dim = start_dim,
        walk_dist = #walk_dist er,
        jump_dist = #jump_dist er,
        jump_time = #jump_time er,
        fall_dist = #fall_dist er,
        has_gravity = #has_gravity er,
        jump_factor = #jump_factor er,
        anim = Vector.fromList anims })
    end

  fun load_entities root file r =
    let
      val ent = Json.load_file file []
      val k = Json.object_size ent
      fun load name obj i =
        let val r = load_entity_resource obj name r root
        in
          Json.object_set_new obj "index" (Json.integer i);
          r
        end
    in
      Json.object_foreach ent load
    end

  fun int_of_state Idle = 0
    | int_of_state Walk = 1
    | int_of_state Fall = 2
    | int_of_state Jump = 3
    | int_of_state Hang = 4

  fun load_state es =
    let
      val er = #rule es
      val ar = (Vector.sub (#anim er, int_of_state (#st es)))
      val anim = { pos = 0, frame = hd (#frames ar), remaining = hd (#duration ar) }
    in {
      active = #active es,
      pos = #pos es,
      spawn = #spawn es,
      dir = #dir es,
      st = #st es,
      jump_timeout = #jump_timeout es,
      jump_type = #jump_type es,
      fall_time = #fall_time es,
      tex = #tex es,
      rule = er,
      anim = anim,
      hitbox = #box ar }
    end

  fun init_entity_state es er t st =
    let
      val sd = case er of
        SOME r => (#start_dim r) |
        NONE => (#spawn es)
      val w = #w sd
      val h = #h sd
      val rule = case er of
          SOME r => r |
          NONE => #rule es
      val tex = case t of
          SOME te => te |
          NONE => #tex es
    in
      load_state {
        rule = rule,
        tex = tex,
        active = true,
        anim = #anim es,
        dir = Left,
        st = st,
        hitbox = #hitbox es,
        pos = { x = #x (#spawn es), y = #y (#spawn es) },
        spawn = { x = #x (#spawn es), y = #y (#spawn es),
          w = w, h = h },
        jump_timeout = 0,
        jump_type = Jump_High,
        fall_time = 0 }
    end

  fun clear_debug s =
    { active = false, pause = false, frames = true,
      hitboxes = true, show_terrain_collision = false, message_positions = true,
      font = #font s,
      terrain_collision = #terrain_collision s }

  (*** teardown ***)
  fun destroy_level l = ()
end

(* vim: se ai et: *)
