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
    msg : message option,
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
  val int_of_group : group -> int

(*
  (*** functions ***)
  (** high level init **)
  val init_game : string -> (session * game_state)
  val load_config : Json.t -> string -> (session * game_state)
  val load_intro : session -> Json.t -> string -> Engine.entity_rule -> Sdl.texture vector -> Engine.entity_state
  val init_group : Json.t -> Json.t -> string -> Sdl.texture vector -> Engine.entity_rule vector -> Engine.state -> Engine.entity_state vector
*)

  (** collisions **)
  val in_rect : Sdl.point -> Sdl.rect -> bool
  val have_collision : Sdl.rect -> Sdl.rect -> bool

  (** rendering **)
  val render_message : Sdl.renderer -> Sdl.font -> Json.t -> int -> msg_line vector
  val draw_message_boxes : Sdl.renderer -> message vector -> Sdl.rect -> unit
  val render_entity_info : Sdl.renderer -> Sdl.font -> Engine.entity_state -> unit
  val draw_message : Sdl.renderer -> Sdl.texture -> message -> Sdl.rect -> Sdl.rect -> unit

  (** high level game **)
(*
  val process_event : Sdl.event -> game_event
  val update_gamestate : session -> game_event -> game_state -> game_state
  val set_group_state : Engine.state -> Engine.entity_state vector -> Engine.entity_state vector
  val enemy_movement : Engine.level -> Sdl.rect -> Engine.entity_state vector -> Engine.entity_state vector
  *)
  val render : session -> game_state -> unit
  val clear_game : game_state -> game_state
  val clear_event : game_event

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
    msg : message option,
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

  fun int_of_group Group_Objects = 0
    | int_of_group Group_Enemies = 1
    | int_of_group Group_Player = 2

  (*** functions ***)
  (** high level init **)

  (** collisions **)
  fun in_rect p r =
    E.between (#x p) (#x r) ((#x r) + #w r) andalso
      E.between (#y p) (#y r) ((#y r) + #h r)

  fun have_collision r1 r2 =
    let
      fun corners r =
        (#x r, (#x r) + #w r, #y r, (#y r) + #h r)
      val (lf1, rt1, tp1, bt1) = corners r1
      val (lf2, rt2, tp2, bt2) = corners r2
    in
      ((E.between lf1 lf2 rt2) orelse (E.between rt1 lf2 rt2) orelse (E.between lf2 lf1 rt1) orelse (E.between rt2 lf1 rt1)) andalso
        ((E.between tp1 tp2 bt2) orelse (E.between bt1 tp2 bt2) orelse (E.between tp2 tp1 bt1) orelse (E.between bt2 tp1 bt1))
    end

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

  fun draw_message_boxes r msgs screen =
    let
      fun draw msg =
        let
          val light_green = { r = 23, g = 225, b = 38, a = 255 }
          val dark_green = { r = 0, g = 100, b = 0, a = 255 }
          val (fill, col) = case #when msg of
            Msg_Always => (true, light_green) |
            Msg_Never => (false, dark_green) |
            Msg_Once => (true, dark_green)
          val a = 4
          val b = {
            x = #x (#pos msg) - (a div 2) - #x screen,
            y = #y (#pos msg) - (a div 2) - #y screen,
            w = a, h = a }
        in
          Sdl.set_render_draw_color r col;
          if fill then
            Sdl.render_fill_rect r b
          else
            Sdl.render_draw_rect r b
        end
    in
      Vector.app draw msgs
    end

  fun render_entity_info r font e =
    let
      val hb = E.entity_hitbox e
      val ft = E.entity_feet hb
      fun rend i s =
        E.render_line r s font i
    in
      rend 0 ("pos:  " ^ (Int.toString (#x (#pos e))) ^ " " ^ (Int.toString (#y (#pos e))) ^
        ", state: " ^ E.string_of_state (#st e));
      rend 1 ("feet: " ^ (Int.toString (#x ft)) ^ " " ^ (Int.toString (#y ft)));
      rend 2 ("fall time: " ^ (Int.toString (#fall_time e)));
      rend 3 ("jump timeout: " ^ (Int.toString (#jump_timeout e)))
    end

  fun draw_message r t m box line =
    let
      val x = #x box + #x line
      val y = #y box + #y line
      fun draw_line (l, y) =
        let val dst = { x = x, y = y, w = #x (#size l), h = #y (#size l) }
        in
          Sdl.render_copy r (#tex l) NONE (SOME dst);
          y + #h line
        end
    in
      Sdl.render_copy r t NONE (SOME box);
      Vector.foldl draw_line y (#lines m);
      ()
    end

  (** high level game **)
  fun enemy_movement terrain player nmi =
    Vector.map (fn e =>
      let
        val h = E.entity_hitbox e
        val rule = #rule e
        val (turn, right) =
          if (E.between (#y player) (#y h) ((#y h) + #h h)) orelse
              E.between (#y h) (#y player) ((#y player) + #h player) then
            (true, #x (#pos e) < #x player)
          else
            (false, false)
        val dir = if turn then
          (if right then E.Right else E.Left)
          else #dir e
        val jump = (E.between (#x player) (#x h) ((#x h) + #w h)) andalso
            #y (#pos e) > #y player
        val track = turn orelse jump
        val h' = {
          x = (#x h) + ((E.int_of_dir dir) * #walk_dist rule),
          y = #y h, w = #w h, h = #h h }
        val walk = ((E.collides_with_terrain h' terrain) <> E.Hit_None) andalso
            ((not (#has_gravity rule)) orelse E.stands_on_terrain h' terrain)
        val order = {
          move_left = walk andalso (not right),
          move_right = walk andalso right,
          move_jump = jump,
          walk = walk }
        val (mlog, e') = E.move_entity e order terrain
      in
        if (not track) andalso (not walk) then
          E.turn e' (if dir = E.Left then E.Right else E.Left)
        else
          e'
      end) nmi

  fun render s gs =
    let
      val player = Vector.sub (Vector.sub (#entities gs, int_of_group Group_Player), 0)
      val pos = #pos player
      val spawn = #spawn player
      val screen = {
        x = (#x pos) - ((#x (#screen s)) - #w spawn) div 2,
        y = (#y pos) - ((#y (#screen s)) - #h spawn) div 2,
        w = #x (#screen s),
        h = #y (#screen s) }
      val scr = { x = 0, y = 0, w = #w screen, h = #h screen }
      val r = #r s
      fun render_mode Mode_Logo = E.draw_entity r scr (#logo gs) NONE
        | render_mode Mode_Intro = E.draw_entity r scr (#intro gs) NONE
        | render_mode Mode_Game =
          let
            val msg = #msg s
          in
            E.draw_background r (#background (#level s)) screen;
            Vector.app (fn g =>
              Vector.app (fn e =>
                if #active e then
                  E.draw_entity r screen e (SOME (#debug gs))
                else ()) g) (#entities gs);
            E.draw_entity r screen player (SOME (#debug gs));
            case #msg gs of
              SOME m => draw_message r (#tex msg) m (#box msg) (#line msg) |
              NONE => ()
           end
        | render_mode Mode_Exit = print "bye\n"
    in
      Sdl.render_clear r;
      render_mode (#run gs);
      Sdl.render_present r
    end

  fun clear_game gs = {
    need_to_collect = Vector.length (Vector.sub (#entities gs, int_of_group Group_Objects)),
    logo = #logo gs,
    intro = #intro gs,
    entities = #entities gs,
    msg = NONE,
    msg_timeout = 0,
    run = #run gs,
    debug = E.clear_debug (#debug gs) }

  val clear_event = {
    player = E.clear_order,
    exit = false,
    toggle_debug = false,
    toggle_pause = false,
    toggle_terrain = false,
    reload_conf = false,
    keyboard = false,
    reset = false }

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
