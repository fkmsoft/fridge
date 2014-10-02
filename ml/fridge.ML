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

  type game_state = {
    need_to_collect : int,
    logo : Engine.entity_state,
    intro : Engine.entity_state,
    entities : {
      players: Engine.entity_state vector,
      objects: Engine.entity_state vector,
      enemies: Engine.entity_state vector },
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

  (*** functions ***)
  (** high level init **)
  val load_config : Json.t -> string -> (Sdl.point * Sdl.window * Sdl.renderer) -> (session * game_state)
  val load_intro : Sdl.point -> Json.t -> string -> (Sdl.texture * Engine.entity_rule) vector -> Engine.entity_state
  val init_group : Json.t -> Json.t -> string -> (Sdl.texture * Engine.entity_rule) vector -> Engine.state -> Engine.entity_state vector
  val init_game : string -> (session * game_state)

  (** shutdown **)
  val end_session : session -> unit
  val destroy_gamestate : game_state -> unit

  (** collisions **)
  val in_rect : Sdl.point -> Sdl.rect -> bool
  val have_collision : Sdl.rect -> Sdl.rect -> bool

  (** rendering **)
  val render_message : Sdl.renderer -> Sdl.font -> Json.t -> int -> msg_line vector
  val draw_message_boxes : Sdl.renderer -> message vector -> Sdl.rect -> unit
  val render_entity_info : Sdl.renderer -> Sdl.font -> Engine.entity_state -> unit
  val draw_message : Sdl.renderer -> Sdl.texture -> message -> Sdl.rect -> Sdl.rect -> unit

  (** high level game **)
  val clear_event : game_event
  val process_event : Sdl.event_type -> game_event -> game_event
  val set_group_state : Engine.state -> Engine.entity_state vector -> Engine.entity_state vector
  val enemy_movement : Engine.level -> Sdl.rect -> Engine.entity_state vector -> Engine.entity_state vector
  val update_gamestate : session -> game_event -> game_state -> game_state
  val render : session -> game_state -> unit
  val clear_game : game_state -> game_state

  (** utility **)
  val load_finish : Sdl.renderer -> Json.t -> Sdl.font -> int -> finish
  val load_asset_surf : Json.t -> string -> string -> Sdl.surface
  val load_messages : Json.t -> Sdl.font -> int -> string -> Sdl.point -> Sdl.renderer -> msg_info
  val load_collisions : Json.t -> Engine.line vector * Engine.line vector

  (** main **)
  val main : string -> unit
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

  type game_state = {
    need_to_collect : int,
    logo : Engine.entity_state,
    intro : Engine.entity_state,
    entities : {
      players: Engine.entity_state vector,
      objects: Engine.entity_state vector,
      enemies: Engine.entity_state vector },
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

  (*** functions ***)

  (** shutdown **)
  fun end_session s = (
    E.destroy_level (#level s);
    Sdl.destroy_texture (#background (#level s));
    Sdl.destroy_renderer (#r s);
    Sdl.destroy_window (#w s);
    Sdl.quit ())

  fun destroy_gamestate gs =
    case (#font (#debug gs)) of
      SOME font => Sdl.ttf_close_font font |
      NONE => ()

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
          print ("rendering message `" ^ str ^ "'\n");
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
  val clear_event = {
    player = E.clear_order,
    exit = false,
    toggle_debug = false,
    toggle_pause = false,
    toggle_terrain = false,
    reload_conf = false,
    keyboard = false,
    reset = false }

  fun process_event (Sdl.Key_Down _) e = {
      exit = #exit e,
      player = #player e,
      toggle_debug = #toggle_debug e,
      toggle_pause = #toggle_pause e,
      toggle_terrain = #toggle_terrain e,
      reload_conf = #reload_conf e,
      keyboard = true,
      reset = #reset e }
    | process_event Sdl.Quit e = {
      exit = true,
      player = #player e,
      toggle_debug = #toggle_debug e,
      toggle_pause = #toggle_pause e,
      toggle_terrain = #toggle_terrain e,
      reload_conf = #reload_conf e,
      keyboard = #keyboard e,
      reset = #reset e }
    | process_event (Sdl.Key_Up k) e = {
      exit = (k = Sdl.Key_Q orelse #exit e),
      player = {
        move_left = (k = Sdl.Key_Left orelse #move_left (#player e)),
        move_right = (k = Sdl.Key_Right orelse #move_right (#player e)),
        move_jump = (k = Sdl.Key_Space orelse #move_jump (#player e)),
        walk = (k = Sdl.Key_Left orelse k = Sdl.Key_Right orelse #walk (#player e)) },
      toggle_debug = (k = Sdl.Key_D orelse #toggle_debug e),
      toggle_pause = (k = Sdl.Key_P orelse #toggle_pause e),
      toggle_terrain = #toggle_terrain e,
      reload_conf = (k = Sdl.Key_U orelse #reload_conf e),
      keyboard = true,
      reset = (k = Sdl.Key_R orelse #reset e) }
    | process_event _ e = e

  fun set_group_state st g =
    Vector.map (fn e => E.load_state e st) g

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
        val walk = ((E.collides_with_terrain h' terrain) = E.Hit_None) andalso
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

  fun update_gamestate s ev gs =
    let
      fun switch Mode_Logo =
          let
            val logo = E.tick_animation (#logo gs)
            val idle_anim = Vector.sub ((#anim (#rule logo)), E.int_of_state E.Idle)
            val n_frames = Vector.length (#frames idle_anim)
            val skip = (#pos (#anim logo)) = n_frames - 1 orelse #keyboard ev
          in
            if skip then
              switch Mode_Intro
            else
              { logo = logo,
                need_to_collect = #need_to_collect gs,
                intro = #intro gs,
                entities = #entities gs,
                msg = #msg gs,
                msg_timeout = #msg_timeout gs,
                run = Mode_Logo,
                debug = #debug gs }
          end
        | switch Mode_Intro =
          let
            val intro = E.tick_animation (#intro gs)
            val skip = #keyboard ev
          in
            if skip then
              switch Mode_Game
            else
              { intro = intro,
                need_to_collect = #need_to_collect gs,
                logo = #logo gs,
                entities = #entities gs,
                msg = #msg gs,
                msg_timeout = #msg_timeout gs,
                run = Mode_Intro,
                debug = #debug gs }
          end
        | switch Mode_Game =
          let
            val d = #debug gs
            val debug = if #toggle_debug ev then not (#active d) else #active d
            (* TODO reload conf *)
            val pause = if #toggle_pause ev andalso debug then not (#pause d) else #pause d
            val show_terrain_collision = if #toggle_terrain ev andalso debug then not (#show_terrain_collision d) else #show_terrain_collision d
            fun animate e = if #active e then E.tick_animation e else e
            val entities = #entities gs
            val objects = Vector.map animate (#objects entities)
            val players = Vector.map animate (#players entities)
            val player = Vector.sub (players, 0)
            val enemies = Vector.map animate (set_group_state (if debug andalso pause then E.Idle else E.Walk) (#enemies entities))
            val level = #level s
            val moved_enemies = if not debug andalso not pause then
                enemy_movement level (E.entity_hitbox player) enemies
              else
                enemies
            val (mlog, moved_player_) = E.move_entity player (#player ev) level
            val moved_player = E.load_state moved_player_ (#st moved_player_)
            val next_player = if #reset ev then E.init_entity_state moved_player E.Idle else moved_player
            val (old_msg, old_msg_timeout) = if #msg_timeout gs > 0 then
                (#msg gs, (#msg_timeout gs) - 1)
              else
                (NONE, 0)
            val r = E.entity_hitbox next_player
            val mrules = #msg s
            val (msg, msg_timeout) = case (Vector.find (fn m =>
                #when m <> Msg_Never andalso in_rect (#pos m) r) (#msgs mrules)) of
              SOME m => (SOME m, #timeout mrules) (* TODO update Msg_Once status *) |
              NONE => (old_msg, old_msg_timeout)
            (* TODO player-object/-enemy collision w/ collect & death *)
            val run = if #exit ev then Mode_Exit else Mode_Game
          in
            { need_to_collect = #need_to_collect gs,
              intro = #intro gs,
              logo = #logo gs,
              entities = {players = Vector.fromList [next_player], objects = objects, enemies = moved_enemies},
              msg = msg,
              msg_timeout = msg_timeout,
              run = run,
              debug = {
                active = debug,
                frames = #frames d,
                hitboxes = #hitboxes d,
                pause = pause,
                show_terrain_collision = show_terrain_collision,
                terrain_collision = #terrain_collision d,
                message_positions = #message_positions d,
                font = #font d } }
          end
        | switch Mode_Exit = gs
    in
      switch (#run gs)
    end

  fun render s gs =
    let
      val player = Vector.sub (#players (#entities gs), 0)
      val pos = #pos player
      val spawn = #spawn player
      val screen = {
        x = (#x pos) - ((#x (#screen s)) - #w spawn) div 2,
        y = (#y pos) - ((#y (#screen s)) - #h spawn) div 2,
        w = #x (#screen s),
        h = #y (#screen s) }
      val scr = { x = 0, y = 0, w = #w screen, h = #h screen }
      val r = #r s
      fun draw_group g =
        Vector.app (fn e =>
          if #active e then
            E.draw_entity r screen e (SOME (#debug gs))
          else ()) g
      fun render_mode Mode_Logo = E.draw_entity r scr (#logo gs) NONE
        | render_mode Mode_Intro = E.draw_entity r scr (#intro gs) NONE
        | render_mode Mode_Game =
          let
            val msg = #msg s
            val d = #debug gs
          in
            E.draw_background r (#background (#level s)) screen;
            if true then (*(#active d) andalso #show_terrain_collision d then*)
              E.draw_terrain_lines r (#level s) screen
            else
              ();
            draw_group (#objects (#entities gs));
            draw_group (#enemies (#entities gs));
            E.draw_entity r screen player (SOME (#debug gs));
            if #active d then
              case #font d of
                SOME f => render_entity_info r f player |
                NONE => print "no font\n"
            else
              ();
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
    need_to_collect = Vector.length (#objects (#entities gs)),
    logo = #logo gs,
    intro = #intro gs,
    entities = #entities gs,
    msg = NONE,
    msg_timeout = 0,
    run = #run gs,
    debug = E.clear_debug (#font (#debug gs)) (#terrain_collision (#debug gs)) }

  (** utility **)
  fun load_finish r game font fontsize =
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
          lines = render_message r font m 0 } end
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

  fun load_messages game font fontsize root screen r =
    let
      val obj = Json.object_get game "message"
      val msg_srf = load_asset_surf obj root "resource"
      val tex = Sdl.create_texture_from_surface r msg_srf
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
          lines = render_message r font m 3 }
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
      val (hor, ver) = Vector.foldl switch ([], []) lines
    in
      (Vector.fromList hor, Vector.fromList ver)
    end

  (** high level init **)
  fun init_group game entities key e_ress st =
    let
      val objs = Json.object_get game key (* TODO handle failure *)
      fun ival obj = Json.integer_value obj
      fun load name obj i =
        let
          val entity = Json.object_get entities name
          val ei = ival (Json.object_get entity "index")
          fun f spawn j =
            let
              val (tex, rule) = Vector.sub (e_ress, ei)
              val spawn_es = {
                active = false,
                pos = { x = 0, y = 0 },
                hitbox = { x = 0, y = 0, w = 0, h = 0 },
                spawn = {
                  x = ival (Json.array_get spawn 0),
                  y = ival (Json.array_get spawn 1),
                  w = #w (#start_dim rule),
                  h = #h (#start_dim rule) },
                dir = E.Left,
                st = st,
                jump_timeout = 0,
                jump_type = E.Jump_High,
                fall_time = 0,
                anim = { pos = 0, frame = 0, remaining = 0 },
                rule = rule,
                tex = tex }
              val es = E.init_entity_state spawn_es st
              (* TODO custom rules *)
            in
              es
            end
        in
          Json.array_foreach obj f
        end
    in
      Vector.concat (Json.object_foreach objs load)
    end

  fun load_intro screen obj k e_ress =
    let
      val io = Json.object_get obj k (* TODO handle failure -> active = false *)
      val i = Json.integer_value (Json.object_get io "index")
      val w = 640 (* FIXME *)
      val h = 480
      val (tex, rule) = Vector.sub (e_ress, i)
      val intro = {
            active = false,
            pos = { x = 0, y = 0 },
            hitbox = { x = 0, y = 0, w = 0, h = 0 },
            spawn = {
              x = ((#x screen) - w) div 2,
              y = ((#y screen) - h) div 2,
              w = w,
              h = h },
            dir = E.Left,
            st = E.Idle,
            jump_timeout = 0,
            jump_type = E.Jump_High,
            fall_time = 0,
            anim = { pos = 0, frame = 0, remaining = 0 },
            rule = rule,
            tex = tex }
    in
      E.init_entity_state intro E.Idle
    end

  fun load_config game root (screen, w, r) =
    let
      val fnt = Json.object_get game "font"
      val fnt_path = root ^ "/" ^ E.asset_dir ^ "/" ^ (Json.string_value (Json.object_get fnt "resource"))
      val fnt_siz = Json.integer_value (Json.object_get fnt "size")
      val font = Sdl.ttf_open_font fnt_path fnt_siz
      val debug_font = (SOME (Sdl.ttf_open_font "debug_font.ttf" 14)) handle Load_Err => NONE
      val finish = load_finish r game font fnt_siz
      val msg = load_messages game font fnt_siz root screen r
      val lvl = Json.object_get game "level"
      val lvl_path = root ^ "/" ^ E.conf_dir ^ "/" ^ (Json.string_value lvl)
      val level = Json.load_file lvl_path []
      val level_background = E.load_asset_tex level root r "resource"
      val (horizontal, vertical) = load_collisions level
      val entity_res = Json.object_get game "entities"
      val entity_file = Json.string_value (Json.object_get entity_res "resource")
      val entity_path = root ^ "/" ^ E.conf_dir ^ "/" ^ entity_file
      val () = print ("loading entities from: `" ^ entity_path ^ "'\n")
      val (entities, e_ress) = E.load_entities root entity_path r
      val players = init_group game entities "players" e_ress E.Idle
      val objects = init_group game entities "objects" e_ress E.Idle
      val enemies = init_group game entities "enemies" e_ress E.Walk
      val intro = load_intro screen entities "intro" e_ress
      val logo = load_intro screen entities "logo" e_ress
      val terrain_collision = E.load_texture r (root ^ "/assets/level1.gif") (* FIXME *)
    in
      Sdl.ttf_close_font font;
      Json.decref level;
      Json.decref entities;
      Json.decref game;
      ({ w = w,
         r = r,
         level = {
           background = level_background,
           vertical = vertical,
           horizontal = horizontal,
           dim = { x = 0, y = 0, w = 0, h = 0 } (* FIXME *) },
         msg = msg,
         finish = finish,
         screen = screen },
       { need_to_collect = 0,
         logo = logo,
         intro = intro,
         entities = {players = players, objects = objects, enemies = enemies},
         msg = NONE,
         msg_timeout = 0,
         run =
           if #active logo then
             Mode_Logo
           else if #active intro then
             Mode_Intro
           else
             Mode_Game,
         debug = E.clear_debug debug_font terrain_collision})
    end

  fun init_game root =
    let
      val path = root ^ "/" ^ E.conf_dir ^ "/" ^ game_conf
      val game = Json.load_file path []
      val res = Json.object_get game "resolution"
      val x = Json.integer_value (Json.array_get res 0)
      val y = Json.integer_value (Json.array_get res 1)
      val screen = { x = x, y =y }
      fun init () =
        let
          val w = Sdl.create_window "Fridge Filler" Sdl.Windowpos_Undefined Sdl.Windowpos_Undefined x y []
          val icon_path = root ^ "/" ^ "icon.gif"
          val ico = Sdl.img_load icon_path
          val r = Sdl.create_renderer w ~1 0
          val (s, gs) = load_config game root (screen, w, r)
        in
          Sdl.set_window_icon w ico;
          Sdl.free_surface ico;
          (s, clear_game gs)
        end
    in
      Sdl.ttf_init ();
      Sdl.init [Sdl.Init_Video];
      init ()
    end

    (** main **)
    fun main root =
      let
        val (s, gs) = init_game root
        fun game_tick gs ge = update_gamestate s ge gs
        fun loop old_ticks old_ge gs =
          let
            val ticks = Sdl.get_ticks ()
            val ge = case Sdl.poll_event () of
              SOME ev => process_event (#t ev) old_ge |
              NONE => old_ge
            val slept = (ticks - old_ticks)
            val (ticks', gs', ge') = if slept >= tick then
                (ticks, game_tick gs ge, clear_event)
              else
                (old_ticks, gs, ge)
          in
            render s gs;
            Sdl.delay (tick div 4);
            if (#run gs') = Mode_Exit then
              ()
            else
              loop ticks' ge' gs'
          end
      in
        loop (Sdl.get_ticks ()) clear_event gs;
        destroy_gamestate gs;
        end_session s
      end
end

(* vim: se ai et: *)
