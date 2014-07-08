signature FRIDGE =
sig
  (*** types ***)
  datatype mode = Mode_Logo | Mode_Intro | Mode_Game | Mode_Exit
  datatype msg_frequency = Msg_Never | Msg_One | Msg_Always

  type message = {
    when : msg_frequency,
    pos : Sdl.point,
    lines : { size : Sdl.point, tex : Sdl.texture } vector }

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

  (** rendering **)
  val render_message : Sdl.renderer -> Sdl.font -> Json.t -> int -> message -> message
  val draw_message_boxes : Sdl.renderer -> msg_info -> Sdl.rect -> unit
  val render_entity_info : Sdl.renderer -> Sdl.font -> Engine.entity_state -> unit
  val draw_message : Sdl.renderer -> Sdl.texture -> message -> Sdl.rect -> Sdl.rect -> unit

  (** utility **)
  val load_finish : Json.t -> Sdl.font -> int -> session -> session
  val load_messages : Json.t -> Sdl.font -> int -> string -> session -> session
  val load_collisions : Json.t -> Engine.level -> Engine.level
  val load_asset_surf : Json.t -> string -> string -> Sdl.surface
end

(* vim: se ai et: *)
