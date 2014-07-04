signature SDL =
sig
  (*** types ***)

  (* opaque *)
  type renderer = CInterface.vol
  type texture = CInterface.vol
  type font = CInterface.vol

  (* structs *)
  type rect = { x : int, y : int, w : int, h : int }
  type point = { x : int, y : int }
  type color = { r : int, g : int, b : int, a : int }
  type surface = { w : int, h : int, data : CInterface.vol }

  (*** constants ***)
  datatype renderer_flip = Flip_None | Flip_Horizontal | Flip_Vertical
  val int_of_flip : renderer_flip -> int

  datatype blend_mode = Blend_Mode_None | Blend_Mode_Blend | Blend_Mode_Add | Blend_Mode_Mod
  val int_of_blend_mode : blend_mode -> int

  (*** functions ***)

  (** textures & surfaces **)
  val create_texture_from_surface : renderer -> surface -> texture
  val free_surface : surface -> unit
  val destroy_texture : texture -> unit

  (** rendering **)
  val set_render_draw_color : renderer -> color -> unit
  val set_render_draw_blend_mode : renderer -> blend_mode -> unit
  val render_draw_line : renderer -> int -> int -> int -> int -> unit
  val render_fill_rect : renderer -> rect -> unit
  val render_copy : renderer -> texture -> rect option -> rect option -> unit
  val render_copy_ex : renderer -> texture -> rect -> rect -> real -> point -> renderer_flip -> unit

  (** SDL_image **)
  val img_load : string -> surface

  (** SDL_ttf **)
  val ttf_render_text_blended : font -> string -> color -> surface
end

structure Sdl : SDL =
struct
  structure C = CInterface

  type renderer = C.vol
  type texture = C.vol
  type font = C.vol

  type rect = { x : int, y : int, w : int, h : int }
  type point = { x : int, y : int }
  type color = { r : int, g : int, b : int, a : int }
  type surface = { w : int, h : int, data : C.vol }

  (*** constants ***)
  val gets = C.get_sym "/usr/lib/libSDL2.so"
  val gets_img = C.get_sym "/usr/lib/libSDL2_image.so"
  val gets_ttf = C.get_sym "/usr/lib/libSDL2_ttf.so"

  datatype renderer_flip = Flip_None | Flip_Horizontal | Flip_Vertical
  fun int_of_flip Flip_None = 0
    | int_of_flip Flip_Horizontal = 1
    | int_of_flip Flip_Vertical = 2

  datatype blend_mode = Blend_Mode_None | Blend_Mode_Blend | Blend_Mode_Add | Blend_Mode_Mod
  fun int_of_blend_mode Blend_Mode_None = 0
    | int_of_blend_mode Blend_Mode_Blend = 1
    | int_of_blend_mode Blend_Mode_Add = 2
    | int_of_blend_mode Blend_Mode_Mod = 4

  exception Null_Pointer

  val void_p = C.Cpointer C.Cvoid

  fun surface_from_c s =
    if C.fromCint s = 0 then
      raise Null_Pointer
    else
      { w = C.fromCint (C.offset 2 C.Cint (C.deref s)),
        h = C.fromCint (C.offset 3 C.Cint (C.deref s)),
        data = s }

  fun surface_to_c s = #data s

  val SURFACE = C.mkConversion surface_from_c surface_to_c void_p

  fun img_load file =
    (C.call1 (gets_img "IMG_Load") C.STRING SURFACE) file

  fun create_texture_from_surface r s =
    (C.call2 (gets "SDL_CreateTextureFromSurface") (C.POINTER, SURFACE) C.POINTER)
      (r, s)

  fun free_surface s =
    (C.call1 (gets "SDL_FreeSurface") SURFACE C.VOID) s

  fun destroy_texture t =
    (C.call1 (gets "SDL_DestroyTexture") C.POINTER C.VOID) t

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
      raise Null_Pointer
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
      raise Null_Pointer
    else
      { x = C.fromCint (C.offset 0 C.Cint (C.deref r)),
        y = C.fromCint (C.offset 1 C.Cint (C.deref r)),
        w = C.fromCint (C.offset 2 C.Cint (C.deref r)),
        h = C.fromCint (C.offset 3 C.Cint (C.deref r)) }

  val RECT = C.mkConversion rect_from_c rect_to_c void_p

  fun render_draw_line r x1 y1 x2 y2 =
    (C.call5 (gets "SDL_RenderDrawLine") (C.POINTER, C.INT, C.INT, C.INT, C.INT) C.VOID)
      (r, x1, y1, x2, y2)

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

  fun ttf_render_text_blended font text { r, g, b, a } =
    (C.call6 (gets_ttf "TTF_RenderText_Blended") (C.POINTER, C.STRING, C.INT, C.INT, C.INT, C.INT) SURFACE)
      (font, text, r, g, b, a)
end

(* vim: se ai et: *)
