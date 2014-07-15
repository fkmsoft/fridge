signature SDL =
sig
  (*** types ***)

  (* opaque *)
  type window = CInterface.vol
  type renderer = CInterface.vol
  type texture = CInterface.vol
  type font = CInterface.vol

  (*** constants ***)
  datatype key = Key_Unkown | Key_Left | Key_Right | Key_Space | Key_Q | Key_B | Key_U | Key_P | Key_R | Key_D

  datatype renderer_flip = Flip_None | Flip_Horizontal | Flip_Vertical

  datatype blend_mode = Blend_Mode_None | Blend_Mode_Blend | Blend_Mode_Add | Blend_Mode_Mod

  datatype window_flag = Window_Fullscreen | Window_OpenGL | Window_Shown | Window_Hidden | Window_Borderless | Window_Resizable | Window_Minimized | Window_Maximized
  datatype window_pos = Windowpos_Undefined | Windowpos_Centered | Windowpos of int

  datatype init_flag = Init_Timer | Init_Audio | Init_Video | Init_Joystick | Init_Haptic | Init_Gamecontroller | Init_Events | Init_Noparachute
  val Init_Everything : init_flag list

  datatype event_type = Quit | Key_Down of key | Key_Up of key | User_Event

  (* records *)
  type rect = { x : int, y : int, w : int, h : int }
  type point = { x : int, y : int }
  type color = { r : int, g : int, b : int, a : int }
  type surface = { w : int, h : int, data : CInterface.vol }
  type event = { t : event_type, data : CInterface.vol }

  (*** functions ***)
  val make_event : unit -> event

  (** init  & shutdown **)
  val init : init_flag list -> unit
  val create_window : string -> window_pos -> window_pos -> int -> int -> window_flag list -> window
  val create_renderer : window -> int -> int -> renderer
  val set_window_icon : window -> surface -> unit

  val destroy_renderer : renderer -> unit
  val destroy_window : window -> unit
  val quit : unit -> unit

  (** time & events **)
  val get_ticks : unit -> int
  val delay : int -> unit
  val poll_event : unit -> event option

  (** textures & surfaces **)
  val create_texture_from_surface : renderer -> surface -> texture
  val free_surface : surface -> unit
  val destroy_texture : texture -> unit

  (** rendering **)
  val render_clear : renderer -> unit
  val render_present : renderer -> unit

  val set_render_draw_color : renderer -> color -> unit
  val set_render_draw_blend_mode : renderer -> blend_mode -> unit

  val render_draw_line : renderer -> int -> int -> int -> int -> unit
  val render_draw_rect : renderer -> rect -> unit
  val render_fill_rect : renderer -> rect -> unit
  val render_copy : renderer -> texture -> rect option -> rect option -> unit
  val render_copy_ex : renderer -> texture -> rect -> rect -> real -> point -> renderer_flip -> unit

  (** SDL_image **)
  val img_load : string -> surface

  (** SDL_ttf **)
  val ttf_init : unit -> unit
  val ttf_open_font : string -> int -> font
  val ttf_close_font : font -> unit
  val ttf_render_text_blended : font -> string -> color -> surface
end

structure Sdl : SDL =
struct
  structure C = CInterface

  type window = C.vol
  type renderer = C.vol
  type texture = C.vol
  type font = C.vol
  type event = C.vol

  type rect = { x : int, y : int, w : int, h : int }
  type point = { x : int, y : int }
  type color = { r : int, g : int, b : int, a : int }
  type surface = { w : int, h : int, data : C.vol }

  (*** constants ***)
  val gets = C.get_sym "/usr/lib/libSDL2.so"
  val gets_img = C.get_sym "/usr/lib/libSDL2_image.so"
  val gets_ttf = C.get_sym "/usr/lib/libSDL2_ttf.so"

  datatype key = Key_Unkown | Key_Left | Key_Right | Key_Space | Key_Q | Key_B | Key_U | Key_P | Key_R | Key_D

  datatype renderer_flip = Flip_None | Flip_Horizontal | Flip_Vertical
  fun int_of_flip Flip_None = 0
    | int_of_flip Flip_Horizontal = 1
    | int_of_flip Flip_Vertical = 2

  datatype blend_mode = Blend_Mode_None | Blend_Mode_Blend | Blend_Mode_Add | Blend_Mode_Mod
  fun int_of_blend_mode Blend_Mode_None = 0
    | int_of_blend_mode Blend_Mode_Blend = 1
    | int_of_blend_mode Blend_Mode_Add = 2
    | int_of_blend_mode Blend_Mode_Mod = 4

  datatype window_flag = Window_Fullscreen | Window_OpenGL | Window_Shown | Window_Hidden | Window_Borderless | Window_Resizable | Window_Minimized | Window_Maximized
  fun int_of_window_flag Window_Fullscreen = 0x1
    | int_of_window_flag Window_OpenGL = 0x2
    | int_of_window_flag Window_Shown = 0x4
    | int_of_window_flag Window_Hidden = 0x8
    | int_of_window_flag Window_Borderless = 0x10
    | int_of_window_flag Window_Resizable = 0x20
    | int_of_window_flag Window_Minimized = 0x40
    | int_of_window_flag Window_Maximized = 0x80

  datatype window_pos = Windowpos_Undefined | Windowpos_Centered | Windowpos of int
  fun int_of_window_pos Windowpos_Undefined = 0x1fff0000
    | int_of_window_pos Windowpos_Centered = 0x2fff0000
    | int_of_window_pos (Windowpos p) = p

  datatype init_flag = Init_Timer | Init_Audio | Init_Video | Init_Joystick | Init_Haptic | Init_Gamecontroller | Init_Events | Init_Noparachute
  val Init_Everything = [Init_Timer, Init_Audio, Init_Video, Init_Events, Init_Joystick, Init_Haptic, Init_Gamecontroller]
  fun int_of_init_flag Init_Timer = 0x1
    | int_of_init_flag Init_Audio = 0x10
    | int_of_init_flag Init_Video = 0x20
    | int_of_init_flag Init_Joystick = 0x200
    | int_of_init_flag Init_Haptic = 0x1000
    | int_of_init_flag Init_Gamecontroller = 0x2000
    | int_of_init_flag Init_Events = 0x4000
    | int_of_init_flag Init_Noparachute = 0x100000

  datatype event_type = Quit | Key_Down of key | Key_Up of key | User_Event
  fun int_of_event_type Quit = 0x100
    | int_of_event_type (Key_Down _) = 0x300
    | int_of_event_type (Key_Up _) = 0x301
    | int_of_event_type User_Event = 0x8000

  val scancode_offset = 0x40000000 (* 1<<30 *)

  fun key_of_keycode 0x20 = Key_Space
    | key_of_keycode 0x62 = Key_B
    | key_of_keycode 0x64 = Key_D
    | key_of_keycode 0x70 = Key_P
    | key_of_keycode 0x71 = Key_Q
    | key_of_keycode 0x72 = Key_R
    | key_of_keycode 0x75 = Key_U
    | key_of_keycode i =
      case i - scancode_offset of
        79 => Key_Right |
        80 => Key_Left |
        _ => Key_Unkown

  fun parse_event 0x100 _ = Quit
    | parse_event 0x300 k = Key_Down (key_of_keycode k)
    | parse_event 0x301 k = Key_Up (key_of_keycode k)
    | parse_event _ _ = User_Event

  type event = { t : event_type, data : CInterface.vol }

  exception Null_Pointer of string

  val void_p = C.Cpointer C.Cvoid

  fun surface_from_c s =
    if C.fromCint s = 0 then
      raise Null_Pointer "surface"
    else
      { w = C.fromCint (C.offset 2 C.Cint (C.deref s)),
        h = C.fromCint (C.offset 3 C.Cint (C.deref s)),
        data = s }

  fun surface_to_c s = #data s

  val SURFACE = C.mkConversion surface_from_c surface_to_c void_p

  exception Load_Err of string

  fun img_load file =
    (C.call1 (gets_img "IMG_Load") C.STRING SURFACE) file
    handle (Null_Pointer _) => raise (Load_Err file)

  fun fold_flags to_int fs =
    foldl (fn (x, a) => a + to_int x) 0 fs

  fun init flags =
    (C.call1 (gets "SDL_Init") C.INT C.VOID) (fold_flags int_of_init_flag flags)

  fun create_window name x y w h flags =
    (C.call6 (gets "SDL_CreateWindow") (C.STRING, C.INT, C.INT, C.INT, C.INT, C.INT) C.POINTER)
      (name, int_of_window_pos x, int_of_window_pos y, w, h, (fold_flags int_of_window_flag flags))

  fun create_renderer w flag_a flag_b =
    (C.call3 (gets "SDL_CreateRenderer") (C.POINTER, C.INT, C.INT) C.POINTER)
      (w, flag_a, flag_b)

  fun set_window_icon w i =
    (C.call2 (gets "SDL_SetWindowIcon") (C.POINTER, SURFACE) C.VOID)
      (w, i)
    handle (Null_Pointer s) => raise Null_Pointer (s ^ "icon")

  fun destroy_renderer r =
    (C.call1 (gets "SDL_DestroyRenderer") C.POINTER C.VOID) r

  fun destroy_window w =
    (C.call1 (gets "SDL_DestroyWindow") C.POINTER C.VOID) w

  fun quit () =
    (C.call0 (gets "SDL_Quit") () C.VOID) ()

  fun get_ticks () =
    (C.call0 (gets "SDL_GetTicks") () C.INT) ()

  fun delay ms =
    (C.call1 (gets "SDL_Delay") C.INT C.VOID) ms

  fun make_event () =
    let
      val ce = C.alloc 56 C.Cchar
      val t = User_Event
    in
      C.assign C.Cint (C.offset 0 C.Cint ce) (C.toCint (int_of_event_type t));
      { t = t, data = C.address ce }
    end

  fun event_to_c e = #data e

  fun event_from_c e =
    if C.fromCint e = 0 then
      raise Null_Pointer "event"
    else
      let
        val typ = C.fromCint (C.offset 0 C.Cint (C.deref e))
        val timestamp = C.fromCint (C.offset 1 C.Cint (C.deref e))
        val window_id = C.fromCint (C.offset 2 C.Cint (C.deref e))
        val flags = C.fromCint (C.offset 3 C.Cint (C.deref e))
        val scancode = C.fromCint (C.offset 4 C.Cint (C.deref e))
        val keycode = C.fromCint (C.offset 5 C.Cint (C.deref e))
        val modifiers = C.fromCshort (C.offset 6 C.Cint (C.deref e))
        val t = parse_event typ keycode
        val () = print ("event: " ^ (case t of
          Quit => "quit\n" |
          Key_Down _ => "keydown\n" |
          Key_Up _ => "keyup\n" |
          User_Event => "user\n"))
      in
        { t = t,
          data = e }
      end

  val EVENT = C.mkConversion event_from_c event_to_c void_p

  fun poll_event () =
    let
      val ev = make_event ()
      val have_event = (C.call1 (gets "SDL_PollEvent") EVENT C.INT) ev
    in
      if have_event = 0 then
        NONE
      else
        SOME (event_from_c (#data ev))
    end

  fun create_texture_from_surface r s =
    (C.call2 (gets "SDL_CreateTextureFromSurface") (C.POINTER, SURFACE) C.POINTER)
      (r, s)
    handle (Null_Pointer s) => raise Null_Pointer (s ^ "texture")

  fun free_surface s =
    (C.call1 (gets "SDL_FreeSurface") SURFACE C.VOID) s
    handle (Null_Pointer s) => raise Null_Pointer (s ^ "free")

  fun destroy_texture t =
    (C.call1 (gets "SDL_DestroyTexture") C.POINTER C.VOID) t

  fun render_clear r =
    (C.call1 (gets "SDL_RenderClear") C.POINTER C.VOID) r

  fun render_present r =
    (C.call1 (gets "SDL_RenderPresent") C.POINTER C.VOID) r

  fun set_render_draw_color rend { r, g, b, a } =
    (C.call5 (gets "SDL_SetRenderDrawColor") (C.POINTER, C.INT, C.INT, C.INT, C.INT) C.VOID)
      (rend, r, g, b, a)

  fun set_render_draw_blend_mode r m =
    (C.call2 (gets "SDL_SetRenderDrawBlendMode") (C.POINTER, C.INT) C.VOID)
      (r, int_of_blend_mode m)

  fun point_to_c p =
    let
      val cp = C.alloc 2 C.Cint
      val x = C.toCint (#x p)
      val y = C.toCint (#y p)
    in
      C.assign C.Cint (C.offset 0 C.Cint cp) x;
      C.assign C.Cint (C.offset 1 C.Cint cp) y;
      C.address cp
    end

  fun point_from_c p =
    if C.fromCint p = 0 then
      raise Null_Pointer "point"
    else
      { x = C.fromCint (C.offset 0 C.Cint (C.deref p)),
        y = C.fromCint (C.offset 1 C.Cint (C.deref p)) }

  val POINT = C.mkConversion point_from_c point_to_c void_p

  fun rect_to_c r =
    let
      val cr = C.alloc 4 C.Cint
      val x = C.toCint (#x r)
      val y = C.toCint (#y r)
      val w = C.toCint (#w r)
      val h = C.toCint (#h r)
    in
      C.assign C.Cint (C.offset 0 C.Cint cr) x;
      C.assign C.Cint (C.offset 1 C.Cint cr) y;
      C.assign C.Cint (C.offset 2 C.Cint cr) w;
      C.assign C.Cint (C.offset 3 C.Cint cr) h;
      C.address cr
    end

  fun rect_from_c r =
    if C.fromCint r = 0 then
      raise Null_Pointer "rect"
    else
      { x = C.fromCint (C.offset 0 C.Cint (C.deref r)),
        y = C.fromCint (C.offset 1 C.Cint (C.deref r)),
        w = C.fromCint (C.offset 2 C.Cint (C.deref r)),
        h = C.fromCint (C.offset 3 C.Cint (C.deref r)) }

  val RECT = C.mkConversion rect_from_c rect_to_c void_p

  fun render_draw_line r x1 y1 x2 y2 =
    (C.call5 (gets "SDL_RenderDrawLine") (C.POINTER, C.INT, C.INT, C.INT, C.INT) C.VOID)
      (r, x1, y1, x2, y2)

  fun render_draw_rect r rect =
    (C.call2 (gets "SDL_RenderDrawRect") (C.POINTER, RECT) C.VOID) (r, rect)

  fun render_fill_rect r rect =
    (C.call2 (gets "SDL_RenderFillRect") (C.POINTER, RECT) C.VOID) (r, rect)

  fun render_copy r t src dst =
    let
      val lower_rc = C.call4 (gets "SDL_RenderCopy") (C.POINTER, C.POINTER, C.POINTER, C.POINTER) C.VOID
      fun maybe_loadrect NONE = C.toCint 0
        | maybe_loadrect (SOME r) = rect_to_c r
      val c_src = maybe_loadrect src
      val c_dst = maybe_loadrect dst
    in
      lower_rc (r, t, c_src, c_dst)
    end

  fun render_copy_ex r t src dst angle center flip =
    let
      val lower_rc = C.call7 (gets "SDL_RenderCopyEx") (C.POINTER, C.POINTER, RECT, RECT, C.DOUBLE, POINT, C.INT) C.VOID
    in
      lower_rc (r, t, src, dst, angle, center, int_of_flip flip)
    end

  fun ttf_init () =
    (C.call0 (gets_ttf "TTF_Init") () C.VOID) ()

  fun ttf_open_font file size =
    let
      val f = (C.call2 (gets_ttf "TTF_OpenFont") (C.STRING, C.INT) C.POINTER) (file, size)
    in
      if C.fromCint f = 0 then
        raise Load_Err file
      else
        f
    end

  fun ttf_close_font font =
    (C.call1 (gets_ttf "TTF_CloseFont") C.POINTER C.VOID) font

  fun ttf_render_text_blended font text { r, g, b, a } =
    let val text = if text = "" then "foo" else text in
    (C.call6 (gets_ttf "TTF_RenderText_Blended") (C.POINTER, C.STRING, C.INT, C.INT, C.INT, C.INT) SURFACE)
      (font, text, r, g, b, a)
    handle (Null_Pointer s) => raise Null_Pointer (s ^ "render: " ^ text)
    end
end

(* vim: se ai et: *)
