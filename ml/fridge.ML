signature FRIDGE =
sig
  (*** types ***)
  datatype mode = Mode_Logo | Mode_Intro | Mode_Game | Mode_Exit
  datatype msg_frequency = Msg_Never | Msg_Once | Msg_Always

  type msg_line = { size : Sdl.point, tex : Sdl.texture }
  type message = {
    when : msg_frequency,
    pos : Sdl.point,
    lines : msg_line vector }

  type msg_info = {
    n : int,
    timeout : int,
    tex : Sdl.texture,
    msgs : message vector,
    box : Sdl.rect,
    line : Sdl.rect }

  type finish = {
    pos : Sdl.point,
    win : message,
    loss : message }

  type session = {
    w : Sdl.window,
    r : Sdl.renderer,
    level : Engine.level,
    msg : msg_info,
    finish : finish,
    screen : Sdl.point }

  datatype group = Group_Player | Group_Objects | Group_Enemies

  type game_state = {
    need_to_collect : int,
    logo : Engine.entity_state,
    intro : Engine.entity_state,
    entities : Engine.entity_state vector vector,
    msg : message,
    msg_timeout : int,
    run : mode,
    debug : Engine.debug_state }

  type game_event = {
    player : Engine.entity_event,
    toggle_pause : bool,
    toggle_debug : bool,
    toggle_terrain : bool,
    reload_conf : bool,
    exit : bool,
    keyboard : bool,
    reset : bool }

  (*** constants ***)
  val tick : int
  val msg_lines : int
  val rootvar : string
  val game_conf : string
  val string_of_frq : msg_frequency -> string

(*
  (*** functions ***)
  (** high level init **)
  val init_game : string -> (session * game_state)
  val load_config : Json.t -> string -> (session * game_state)
  val load_intro : session -> Json.t -> string -> Engine.entity_rule -> Sdl.texture vector -> Engine.entity_state
  val init_group : Json.t -> Json.t -> string -> Sdl.texture vector -> Engine.entity_rule vector -> Engine.state -> Engine.entity_state vector

  (** high level game **)
  val process_event : Sdl.event -> game_event
  val update_gamestate : session -> game_event -> game_state -> game_state
  val set_group_state : Engine.state -> Engine.entity_state vector -> Engine.entity_state vector
  val enemy_movement : Engine.level -> Engine.entity_state -> Engine.entity_state vector -> Engine.entity_state vector
  val render : session -> game_state -> unit
  val clear_game : game_state
  val clear_event : game_event

  (** collisions **)
  val in_rect : Sdl.point -> Sdl.rect -> bool
  val have_collision : Sdl.rect -> Sdl.rect -> bool
*)

  (** rendering **)
  val render_message : Sdl.renderer -> Sdl.font -> Json.t -> int -> msg_line vector
(*
  val draw_message_boxes : Sdl.renderer -> msg_info -> Sdl.rect -> unit
  val render_entity_info : Sdl.renderer -> Sdl.font -> Engine.entity_state -> unit
  val draw_message : Sdl.renderer -> Sdl.texture -> message -> Sdl.rect -> Sdl.rect -> unit
*)

  (** utility **)
  val load_finish : session -> Json.t -> Sdl.font -> int -> finish
  val load_asset_surf : Json.t -> string -> string -> Sdl.surface
  val load_messages : Json.t -> Sdl.font -> int -> string -> session -> msg_info
  val load_collisions : Json.t -> Engine.line list * Engine.line list
end

structure Fridge : FRIDGE =
struct
  structure E = Engine

  (*** types ***)
  datatype mode = Mode_Logo | Mode_Intro | Mode_Game | Mode_Exit
  datatype msg_frequency = Msg_Never | Msg_Once | Msg_Always

  type msg_line = { size : Sdl.point, tex : Sdl.texture }
  type message = {
    when : msg_frequency,
    pos : Sdl.point,
    lines : msg_line vector }

  type msg_info = {
    n : int,
    timeout : int,
    tex : Sdl.texture,
    msgs : message vector,
    box : Sdl.rect,
    line : Sdl.rect }

  type finish = {
    pos : Sdl.point,
    win : message,
    loss : message }

  type session = {
    w : Sdl.window,
    r : Sdl.renderer,
    level : Engine.level,
    msg : msg_info,
    finish : finish,
    screen : Sdl.point }

  datatype group = Group_Player | Group_Objects | Group_Enemies

  type game_state = {
    need_to_collect : int,
    logo : Engine.entity_state,
    intro : Engine.entity_state,
    entities : Engine.entity_state vector vector,
    msg : message,
    msg_timeout : int,
    run : mode,
    debug : Engine.debug_state }

  type game_event = {
    player : Engine.entity_event,
    toggle_pause : bool,
    toggle_debug : bool,
    toggle_terrain : bool,
    reload_conf : bool,
    exit : bool,
    keyboard : bool,
    reset : bool }

  (*** constants ***)
  val tick = 40
  val msg_lines = 2
  val rootvar = "FRIDGE_ROOT"
  val game_conf = "game.json"

  fun string_of_frq Msg_Always = "always"
    | string_of_frq Msg_Never = "never"
    | string_of_frq Msg_Once = "once"

  (*** functions ***)
  (** high level init **)
  (** high level game **)
  (** collisions **)
  (** rendering **)
  fun render_message r font m offset =
    let
      val black = { r = 0, g = 0, b = 0, a = 255 }
      fun render_line i =
        let
          val str = Json.string_value (Json.array_get m (offset + i))
          val text = Sdl.ttf_render_text_blended font str black
          val size = { x = #w text, y = #h text }
          val tex = Sdl.create_texture_from_surface r text
        in
          Sdl.free_surface text;
          { size = size, tex = tex }
        end
    in
      Vector.tabulate (msg_lines, render_line)
    end

  (** utility **)
  fun load_finish s game font fontsize =
    let
      val fin = Json.object_get game "finish"
      val a_pos = Json.object_get fin "pos"
      val pos = {
        x = Json.integer_value (Json.array_get a_pos 0),
        y = Json.integer_value (Json.array_get a_pos 1) }
      fun make_msg key =
        let val m = Json.object_get fin key
        in {
          pos = pos, when = Msg_Never,
          lines = render_message (#r s) font m 0 } end
      val win = make_msg "win"
      val loss = make_msg "loss"
    in
      { pos = pos, win = win, loss = loss }
    end

  fun load_asset_surf a root k =
    let
      val f = E.get_asset a k
      val p = root ^ "/" ^ E.asset_dir ^ "/" ^ f
    in
      Sdl.img_load p
    end

  fun load_messages game font fontsize root s =
    let
      val obj = Json.object_get game "message"
      val mi = #msg s
      val msg_srf = load_asset_surf obj root "resource"
      val tex = Sdl.create_texture_from_surface (#r s) msg_srf
      val screen = #screen s
      val box = {
        x = ((#x screen) - #w msg_srf) div 2,
        y = (#y screen) - #h msg_srf,
        w = #w msg_srf,
        h = #h msg_srf }
      val pos = Json.object_get obj "text-pos"
      val line = {
        x = Json.integer_value (Json.array_get pos 0),
        y = Json.integer_value (Json.array_get pos 1),
        w = fontsize,
        h = 0 }
      val timeout = Json.integer_value (Json.object_get obj "timeout")
      val mo = Json.object_get game "messages"
      val k = Json.array_size mo
      fun f m i =
        { pos = {
            x = Json.integer_value (Json.array_get m 0),
            y = Json.integer_value (Json.array_get m 1) },
          when = if string_of_frq Msg_Once = Json.string_value (Json.array_get m 2)
            then Msg_Once
            else Msg_Always,
          lines = render_message (#r s) font m 3 }
      val ms = Json.array_foreach mo f
    in
      Sdl.free_surface msg_srf;
      { tex = tex,
        box = box,
        line = line,
        timeout = timeout,
        n = k,
        msgs = ms }
    end

  exception Diagonal_Collision_Line of (int * int * int * int)
  datatype lin = Vertical of E.line | Horizontal of E.line
  fun load_collisions lev =
    let
      val lines_o = Json.object_get lev "collision-lines"
      fun f l i =
        let
          val ax = Json.integer_value (Json.array_get l 0)
          val ay = Json.integer_value (Json.array_get l 1)
          val bx = Json.integer_value (Json.array_get l 2)
          val by = Json.integer_value (Json.array_get l 3)
        in
          if ax = bx then
            Vertical { p = ax, a = ay, b = by }
          else if ay = by then
            Horizontal { p = ay, a = ax, b = bx }
          else
            raise Diagonal_Collision_Line (ax, ay, bx, by)
        end
      val lines = Json.array_foreach lines_o f
      fun switch (Horizontal l, (h, v)) = (l::h, v)
        | switch (Vertical l, (h, v)) = (h, l::v)
    in
      Vector.foldl switch ([], []) lines
    end
end

(* vim: se ai et: *)
