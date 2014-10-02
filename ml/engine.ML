signature ENGINE =
sig
  datatype dir = Left | Right
  datatype hit = Hit_None | Hit_Top | Hit_Left | Hit_Right | Hit_Bot
  datatype state = Idle | Walk | Fall | Jump | Hang

  type point = { x : int, y : int }
  type line = { p : int, a : int, b : int }
  type level = {
    background : Sdl.texture,
    dim : Sdl.rect,
    vertical : line vector,
    horizontal : line vector }

  type animation_rule = {
    frames : int vector,
    duration : int vector,
    box : Sdl.rect }

  type entity_rule = {
    start_dim : Sdl.rect,
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
    hitbox : Sdl.rect,
    spawn : Sdl.rect,
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
    font : Sdl.font option }

  (*** magic constants ***)
  val asset_dir : string
  val conf_dir : string
  val string_of_state : state -> string
  val int_of_dir : dir -> int
  val int_of_state : state -> int

  (*** loading ***)
  val load_anim : Json.t -> string -> string -> animation_rule
  val load_asset_tex : Json.t -> string -> Sdl.renderer -> string -> Sdl.texture
  val load_entity_rule : Json.t -> string -> entity_rule
  val load_entities : string -> string -> Sdl.renderer -> (Json.t * (Sdl.texture * entity_rule) vector)
  val load_entity_resource : Json.t -> string -> Sdl.renderer -> string -> Sdl.texture * entity_rule
  val load_state : entity_state -> state -> entity_state
  val init_entity_state : entity_state -> state -> entity_state
  val clear_debug : Sdl.font option -> Sdl.texture -> debug_state

  (*** teardown ***)
  val destroy_level : level -> unit

  (*** state updates ***)
  val clear_order : entity_event
  val tick_animation : entity_state -> entity_state
  val kick_entity : entity_state -> hit list -> point -> entity_state

  (*** movement ***)
  val keystate_to_movement : Sdl.key list -> entity_event
  val turn : entity_state -> dir -> entity_state
  val move_entity : entity_state -> entity_event -> level -> move_log * entity_state

  (*** collision ***)
  val between : int -> int -> int -> bool
  val collides_with_terrain : Sdl.rect -> level -> hit
  val stands_on_terrain : Sdl.rect -> level -> bool
  val entity_hitbox : entity_state -> Sdl.rect
  val cmp_lines : unit -> unit
  val entity_feet : Sdl.rect -> point
  
  (*** rendering ***)
  val draw_background : Sdl.renderer -> Sdl.texture -> Sdl.rect -> unit
  val draw_terrain_lines : Sdl.renderer -> level -> Sdl.rect -> unit
  val render_line : Sdl.renderer -> string -> Sdl.font -> int -> unit
  val draw_entity : Sdl.renderer -> Sdl.rect -> entity_state -> debug_state option -> unit

  (*** Json utility ***)
  val get_asset : Json.t -> string -> string
  val get_int_field : Json.t -> string -> string -> int
  val get_float_field : Json.t -> string -> string -> real

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

  fun int_of_dir Left = ~1
    | int_of_dir Right = 1

  type point = { x : int, y : int }
  type line = { p : int, a : int, b : int }
  type level = {
    background : Sdl.texture,
    dim : Sdl.rect,
    vertical : line vector,
    horizontal : line vector }

  type animation_rule = {
    frames : int vector,
    duration : int vector,
    box : Sdl.rect }

  type entity_rule = {
    start_dim : Sdl.rect,
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
    hitbox : Sdl.rect,
    spawn : Sdl.rect,
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
    font : Sdl.font option }

  (*** magic constants ***)
  val asset_dir = "assets"
  val conf_dir = "conf"
  fun string_of_state Idle = "idle"
    | string_of_state Walk = "walk"
    | string_of_state Fall = "fall"
    | string_of_state Jump = "jump"
    | string_of_state Hang = "hang"

  (*** Json utility ***)
  exception Load_Err of string

  fun get_asset a k =
    Json.string_value (Json.object_get a k)

  exception Custom

  fun get_int_field obj n s =
    case Json.object_find obj s of
      SOME v => Json.integer_value v |
      NONE =>
        if s = "custom-rule" then
          raise Custom
        else
          (* TODO print ("no int: " ^ s ^ "\n"); *)
          0

  fun get_float_field obj n s =
    case Json.object_find obj s of
      SOME v => Json.real_value v |
      NONE =>
        if s = "custom-rule" then
          raise Custom
        else
          (* TODO print ("no float: " ^ s ^ "\n"); *)
          0.0

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
      val obj = Json.object_get src key
        handle Json.Key_Error k => raise Load_Err ("no " ^ k ^ " animation")
      val frames = Json.object_get obj "frames"
      val dur = Json.object_get obj "duration"
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
          val box = load_rect (Json.object_get obj "box")
        in
          { frames = Vector.fromList frs, duration = Vector.fromList durs, box = box }
        end
    in
      if (Json.array_size frames) <> (Json.array_size dur) then
        raise Load_Err "frames <> durations"
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
      val () = print ("loading " ^ n ^ "\n")
    in {
      walk_dist = i "walk-dist",
      jump_dist = (i "jump-dist-x", i "jump-dist-y"),
      jump_time = i "jump-time",
      fall_dist = i "fall-dist",
      jump_factor = (f "wide-jump-factor", f "high-jump-factor"),
      has_gravity = case Json.object_find src "has-gravity" of
        SOME s => Json.string_value s = "yes" |
        NONE => true,
      anim = Vector.fromList [],
      start_dim = { x = 0, y = 0, w = 0, h = 0 }}
     end

  fun load_entity_resource src n r root =
    let
      val obj = Json.object_get src "resource"
      val ps = Json.string_value obj
      val path = root ^ "/" ^ conf_dir ^ "/" ^ ps
      val er = load_entity_rule src n
      val res = Json.load_file path []
      val t = load_asset_tex res root r "asset"
      val siz = Json.object_get res "frame_size"
      fun i x = Json.integer_value (Json.array_get siz x)
      val start_dim = { x = 0, y = 0, w = i 0, h = i 1 }
      val idle_anim = load_anim res n (string_of_state Idle)
      val states = [Walk, Jump, Fall, Hang]
      val anims = Vector.fromList (idle_anim::(map (fn s =>
        load_anim res n (string_of_state s)
        handle Load_Err _ => (print ("warn: no " ^ (string_of_state s) ^ " anim for " ^ n ^ "\n"); idle_anim)) states))
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
        anim = anims })
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
      (ent, Vector.fromList (Json.object_foreach ent load))
    end

  fun int_of_state Idle = 0
    | int_of_state Walk = 1
    | int_of_state Fall = 2
    | int_of_state Jump = 3
    | int_of_state Hang = 4

  fun load_state es st =
    let
      val er = #rule es
      val ar = (Vector.sub (#anim er, int_of_state st))
      val anim = {
        pos = 0,
        frame = Vector.sub (#frames ar, 0),
        remaining = Vector.sub (#duration ar, 0) }
    in {
      active = #active es,
      pos = #pos es,
      spawn = #spawn es,
      dir = #dir es,
      st = st,
      jump_timeout = #jump_timeout es,
      jump_type = #jump_type es,
      fall_time = #fall_time es,
      tex = #tex es,
      rule = er,
      anim = anim,
      hitbox = #box ar }
    end

  fun init_entity_state es st =
    load_state {
      rule = #rule es,
      tex = #tex es,
      active = true,
      anim = #anim es,
      dir = Left,
      st = st,
      hitbox = #hitbox es,
      pos = { x = #x (#spawn es), y = #y (#spawn es) },
      spawn = #spawn es,
      jump_timeout = 0,
      jump_type = Jump_High,
      fall_time = 0 } st

  fun clear_debug font terrain_collision =
    { active = false, pause = false, frames = true,
      hitboxes = true, show_terrain_collision = false, message_positions = true,
      font = font,
      terrain_collision = terrain_collision }

  (*** teardown ***)
  fun destroy_level l = ()

  (*** state updates ***)
  val clear_order =
    { move_left = false, move_right = false, move_jump = false, walk = false }

  fun tick_animation es =
    let
      val a = #anim es
      val ar = Vector.sub (#anim (#rule es), int_of_state (#st es))
      val remaining = (#remaining a) - 1
      val a' =
        if remaining < 0 then
          let
            val i = (1 + #pos a) mod (Vector.length (#frames ar))
          in
            { pos = i,
              frame = Vector.sub (#frames ar, i),
              remaining = Vector.sub (#duration ar, i) }
          end
        else
          { pos = #pos a,
            frame = #frame a,
            remaining = remaining }
    in
      { anim = a',
        rule = #rule es,
        tex = #tex es,
        active = #active es,
        dir = #dir es,
        st = #st es,
        hitbox = #hitbox es,
        pos = #pos es,
        spawn = #spawn es,
        jump_timeout = #jump_timeout es,
        jump_type = #jump_type es,
        fall_time = #fall_time es }
    end

  fun kick_entity es h v =
    if List.exists (fn x => x = Hit_Top) h then
      es
    else if List.exists (fn x => x = Hit_Left) h andalso
        List.exists (fn x => x = Hit_Right) h then
      es
    else
      { pos = { x = #x (#pos es) + #x v, y = #y (#pos es) + #y v },
        anim = #anim es,
        rule = #rule es,
        tex = #tex es,
        active = #active es,
        dir = #dir es,
        st = #st es,
        hitbox = #hitbox es,
        spawn = #spawn es,
        jump_timeout = #jump_timeout es,
        jump_type = #jump_type es,
        fall_time = #fall_time es }

  (*** collision ***)
  fun first_idx vec x = 0 (* TODO *)

  fun between x a b =
    x >= a andalso x <= b

  fun pt_on_line p l =
    (#y p = #p l) andalso (between (#x p) (#a l) (#b l))

  fun entity_feet {x, y, w, h} =
    { x = x + w div 2,
      y = y + h }

  fun bsearch v (fail:'a) start in_range pred : 'a =
    let
      exception Bsearch_Found of 'a
      fun iter i =
        if i < Vector.length v then
          if in_range (Vector.sub (v, i)) then
            case pred (Vector.sub (v, i)) of
              SOME r => raise Bsearch_Found r |
              NONE => iter (i + 1)
          else
            fail
        else
          fail
    in
      iter start handle Bsearch_Found r => r
    end

  fun intersects_x { a, b, p } { x, y, w, h } = 
    let
      val x1 = x
      val xm = x + (w div 2)
      val x2 = x + w
      val y1 = y
      val y2 = y + h
    in
      if (between p y1 y2) andalso
          ((between x1 a b) orelse (between xm a b) orelse
           (between a x1 xm) orelse (between b x2 xm)) then
        SOME Hit_Left
      else if (between p y1 y2) andalso
          ((between xm a b) orelse (between x2 a b) orelse
           (between a x1 x2) orelse (between b x1 x2)) then
        SOME Hit_Right
      else
        NONE
    end

  fun intersects_y { a, b, p } { x, y, w, h } = 
    let
      val x1 = x
      val xm = x + (w div 2)
      val x2 = x + w
      val y1 = y
      val y2 = y + h
    in
      if (between p x1 xm) andalso
          ((between y1 a b) orelse (between y2 a b) orelse
           (between a y1 y2) orelse (between b y1 y2)) then
        SOME Hit_Left
      else if (between p xm x2) andalso
          ((between y1 a b) orelse (between y2 a b) orelse
           (between a y1 y2) orelse (between b y1 y2)) then
        SOME Hit_Right
      else
        NONE
    end

  fun collides_with_terrain { x, y, w, h } lev =
    let
      val hb = { x = x, y = y, w = w, h = h - 1 }
      val hor = #horizontal lev
      val ver = #vertical lev
      val hit_hor = bsearch hor Hit_None (first_idx hor y)
          (fn x => #p x <= y + h)
          (fn x => intersects_x x hb)
    in
      case hit_hor of
        Hit_None =>
          bsearch ver Hit_None (first_idx ver x)
              (fn u => #p u <= x + w)
              (fn x => intersects_y x hb) |
        h => h
    end

  fun stands_on_terrain r t =
    let
      val mid = entity_feet r
      val mid_y = #y mid
      val v = #horizontal t
    in
      bsearch v false (first_idx v mid_y)
          (fn x => #p x <= mid_y)
          (fn x => if pt_on_line mid x then SOME true
            else NONE)
    end

  fun entity_hitbox s =
    let
      val hb = #hitbox s
      val x = #x (#pos s)
      val y = #y (#pos s) + #y hb
      val xoff = case #dir s of
        Left => #x hb |
        Right => #w (#spawn s) - (#x hb) - (#w hb)
    in
      { x = x + xoff, y = y, w = #w hb, h = #h hb }
    end

  fun cmp_lines () = ()

  (*** movement ***)
  fun keystate_to_movement keys =
    let
      fun match_keys (Key_Left::ks) { move_left, move_right, walk, move_jump } =
        match_keys ks { move_left = true, walk = true, move_right = move_right, move_jump = move_jump }
        | match_keys (Key_Right::ks) { move_left, move_right, walk, move_jump } =
        match_keys ks { move_right = true, walk = true, move_left = move_left, move_jump = move_jump }
        | match_keys (Key_Space::ks) { move_left, move_right, walk, move_jump } =
        match_keys ks { move_jump = true, move_left = move_left, move_right = move_right, walk = walk }
        | match_keys (_::ks) ev =
          match_keys ks ev
        | match_keys [] ev = ev
    in
      match_keys keys clear_order
    end

  fun sign x = if x < 0 then ~1 else 1

  fun entity_vector_move e { x, y } terrain grav =
    let
      exception Break of int * int
      val n = entity_hitbox e
      val dirx = sign x
      val diry = sign y
      val vx = dirx * x
      val vy = diry * y
      val vmax = if vx > vy then vx else vy
      fun iter i out =
        if i <= vmax then
          let
            val dx = dirx * (i * vx) div vmax
            val dy = diry * (i * vy) div vmax
            val r = {
              x = (#x n) + dx,
              y = (#y n) + dy,
              w = #w n, h = #h n }
            val h = collides_with_terrain r terrain
          in
            if h <> Hit_None then
              raise Break out
            else if grav andalso not (stands_on_terrain r terrain) then
              raise Break out
              (* TODO or kick *)
            else
              iter (i + 1) (dx, dy)
          end
        else
          out
      val (dx, dy) = iter 1 (0, 0) handle Break out => out
    in
      ((dx, dy),
          { anim = #anim e,
            rule = #rule e,
            tex = #tex e,
            active = #active e,
            dir = #dir e,
            st = #st e,
            hitbox = #hitbox e,
            pos = { x = dx + #x (#pos e), y = dy + #y (#pos e) },
            spawn = #spawn e,
            jump_timeout = #jump_timeout e,
            jump_type = #jump_type e,
            fall_time = #fall_time e })
    end

  fun entity_walk e terrain =
    let
      val r = #rule e
      val v = { x = (int_of_dir (#dir e)) * (#walk_dist r), y = 0 }
      val ((x, y), e') = entity_vector_move e v terrain (#has_gravity r)
      val dist = if x < 0 then ~x else x
    in
      (dist, e')
    end

  fun entity_start_jump e terrain t =
    let
      val e' = {
            jump_timeout = #jump_time (#rule e),
            jump_type = t,
            anim = #anim e,
            rule = #rule e,
            tex = #tex e,
            active = #active e,
            dir = #dir e,
            st = #st e,
            hitbox = #hitbox e,
            pos = #pos e,
            spawn = #spawn e,
            fall_time = #fall_time e }
    in
      entity_jump e' terrain (t = Jump_Wide) true
    end
  and entity_jump e terrain walk jump =
    let
      val jumping = (#jump_timeout e) > 0
      fun do_jump r =
        let
          val (jx, jy) = #jump_dist r
          val x = (int_of_dir (#dir e)) *
              (if #jump_type e = Jump_Wide then jx
              else if #has_gravity r then 0
              else if walk then #walk_dist r
              else 0)
          val y = ~1 * (jy + (if #has_gravity r then #jump_timeout e else 0))
          val ((dx, dy), e') = entity_vector_move e { x = x, y = y } terrain false
          val timeout = if dy <> y then 0 else (#jump_timeout e) - 1
        in
          (~dy, {
            jump_timeout = timeout,
            jump_type = #jump_type e,
            anim = #anim e,
            rule = #rule e,
            tex = #tex e,
            active = #active e,
            dir = #dir e,
            st = #st e,
            hitbox = #hitbox e,
            pos = #pos e,
            spawn = #spawn e,
            fall_time = #fall_time e })
        end
    in
      if jumping then
        do_jump (#rule e)
      else if #has_gravity (#rule e) then
        (0, e)
      else if jump then
        entity_start_jump e terrain Jump_High
      else
        (0, e)
    end

  fun entity_fall e terrain walk =
    let
      val fall_time = #fall_time e + 1
      fun pre_fall y = entity_vector_move e { x = 0, y = y } terrain false
      fun real_fall e y_extra =
        let
          val r = #rule e
          val x = if walk then int_of_dir (#dir e) * (#walk_dist r) else 0
          val y = #fall_dist r + y_extra
        in
          entity_vector_move e { x = x, y = y } terrain false
        end
    in
      if #has_gravity (#rule e) then
        let val ((dx, dy), e') = pre_fall 1
        in if (dy <> 1) then
          (0, {
              fall_time = 0,
              jump_timeout = #jump_timeout e,
              jump_type = #jump_type e,
              anim = #anim e,
              rule = #rule e,
              tex = #tex e,
              active = #active e,
              dir = #dir e,
              st = #st e,
              hitbox = #hitbox e,
              pos = #pos e,
              spawn = #spawn e })
        else
          let val ((dx, dy), e'') = real_fall e' fall_time
          in (dy, {
              fall_time = fall_time,
              jump_timeout = #jump_timeout e'',
              jump_type = #jump_type e'',
              anim = #anim e'',
              rule = #rule e'',
              tex = #tex e'',
              active = #active e'',
              dir = #dir e'',
              st = #st e'',
              hitbox = #hitbox e'',
              pos = #pos e'',
              spawn = #spawn e'' })
          end
        end
      else
        let val ((dx, dy), e') = real_fall e 0
        in (dy, {
            fall_time = fall_time,
            jump_timeout = #jump_timeout e',
            jump_type = #jump_type e',
            anim = #anim e',
            rule = #rule e',
            tex = #tex e',
            active = #active e',
            dir = #dir e',
            st = #st e',
            hitbox = #hitbox e',
            pos = #pos e',
            spawn = #spawn e' })
        end
    end

  fun turn e dir = {
    dir = dir,
    st = #st e,
    fall_time = #fall_time e,
    jump_timeout = #jump_timeout e,
    jump_type = #jump_type e,
    anim = #anim e,
    rule = #rule e,
    tex = #tex e,
    active = #active e,
    hitbox = #hitbox e,
    pos = #pos e,
    spawn = #spawn e }

  fun move_entity e ev lvl =
    let
      val dir = if #move_left ev then Left
        else if #move_right ev then Right
        else #dir e
      fun on_ground () =
        if #move_jump ev then
          let val (j, e') = entity_start_jump (turn e dir) lvl (if #walk ev andalso #has_gravity (#rule e) then Jump_Wide else Jump_High)
          in ((0,j,0), e') end
        else if #walk ev then
          let val (w, e') = entity_walk (turn e dir) lvl
          in ((w,0,0), e') end
        else
          ((0,0,0), (turn e dir))
      fun move_by_state Idle = on_ground ()
        | move_by_state Walk = on_ground ()
        | move_by_state Hang = ((0,0,0), e)
        | move_by_state Jump =
          let val (j, e') = entity_jump (turn e dir) lvl (#walk ev) (#move_jump ev)
          in ((0,j,0), e') end
        | move_by_state Fall =
          if (#has_gravity (#rule e)) then
            let
              val (f, e') = entity_fall (turn e dir) lvl (#walk ev)
              val h = entity_hitbox e'
            in if f = 0 andalso not (stands_on_terrain h lvl) then
              ((0,0,f), kick_entity e' [(collides_with_terrain { x = #x h, y = 1 + #y h, w = #w h, h = #h h } lvl)] { x = ~1, y = 0 })
            else
              ((0,0,f), e')
            end
          else
            if #move_jump ev then
              let val (j, e') = entity_start_jump (turn e dir) lvl Jump_High
              in ((0,j,0), e') end
            else if #walk ev then
              let
                val (w, e') = entity_walk (turn e dir) lvl
                val (f, e'') = entity_fall e' lvl false
              in ((w,0,f), e'') end
            else
              let val (f, e') = entity_fall (turn e dir) lvl false
              in ((0,0,f), e') end
      val ((w,j,f), e') = move_by_state (#st e)
      val st_next =
        if j > 0 then Jump
        else if stands_on_terrain (entity_hitbox e') lvl then
          if w > 0 then Walk
          else Idle
        else Fall
    in
      ({walked = w,
        jumped = j,
        fallen = f,
        hang = false,
        turned = dir <> #dir e},
       {st = st_next,
        fall_time = #fall_time e',
        jump_timeout = #jump_timeout e',
        jump_type = #jump_type e',
        anim = #anim e',
        rule = #rule e',
        tex = #tex e',
        active = #active e',
        dir = #dir e',
        hitbox = #hitbox e',
        pos = #pos e',
        spawn = #spawn e' })
    end

  (*** rendering ***)
  fun draw_background r bg screen =
    Sdl.render_copy r bg (SOME screen) NONE

  fun draw_terrain_lines r lev screen =
    let
      val red = { r = 200, g = 20, b = 7, a = 255 }
      val {x, y, ...} = screen
      fun draw_horizontal {p, a, b} =
          Sdl.render_draw_line r (a - x) (p - y) (b - x) (p - y)
      fun draw_vertical {p, a, b} =
          Sdl.render_draw_line r (p - x) (a - y) (p - x) (b - y)
    in
      Sdl.set_render_draw_color r red;
      Vector.app draw_vertical (#vertical lev);
      Vector.app draw_horizontal (#horizontal lev)
    end

  fun render_line r s font l =
    let
      val red = { r = 200, g = 20, b = 7, a = 255 }
      val black = { r = 0, g = 0, b = 0, a = 180 }
      val text = Sdl.ttf_render_text_blended font s red
      val tex = Sdl.create_texture_from_surface r text
      val dest = { x = 0, y = l * (#h text), w = #w text, h = #h text }
    in
      Sdl.free_surface text;
      Sdl.set_render_draw_color r black;
      Sdl.set_render_draw_blend_mode r Sdl.Blend_Mode_Blend;
      Sdl.render_fill_rect r dest;
      Sdl.render_copy r tex NONE (SOME dest);
      Sdl.destroy_texture tex
    end

  fun draw_entity r scr s debug =
    let
      val rl = #rule s
      val {w, h, ...} = #start_dim rl
      val src = {
        x = (#frame (#anim s)) * #w (#spawn s),
        y = 0, w = w, h = h }
      val dst = {
        x = #x (#pos s) - #x scr,
        y = #y (#pos s) - #y scr,
        w = w, h =h }
      val flip = #dir s = Right
      val p = { x = 0, y = 0 }
      val pink = { r = 255, g = 105, b = 180, a = 255 }
      val lime = { r =  23, g = 225, b =  38, a = 255 }
      fun debug_render d =
        (if (#frames d) then
          (Sdl.set_render_draw_color r pink;
          Sdl.render_draw_rect r dst)
        else ();
        if (#hitboxes d) then
          let
            val {x, y, w, h} = entity_hitbox s
            val box = {
              x = x - #x scr,
              y = y - #y scr,
              w = w, h = h }
          in
            Sdl.set_render_draw_color r lime;
            Sdl.render_draw_rect r box
          end
        else ())
    in
      Sdl.render_copy_ex r (#tex s) src dst 0.0 p (if flip then Sdl.Flip_Horizontal else Sdl.Flip_None);
      case debug of
      SOME d =>
        if (#active d) then
          debug_render d
        else () |
      NONE => ()
    end
end

(* vim: se ai et: *)
