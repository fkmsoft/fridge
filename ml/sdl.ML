signature SDL =
sig
  type renderer = CInterface.vol
  type texture = CInterface.vol
  type surface = CInterface.vol
  type font = CInterface.vol

  val img_load : string -> surface
  val create_texture_from_surface : renderer -> surface -> texture
  val free_surface : surface -> unit
end

structure Sdl : SDL =
struct
  structure C = CInterface

  type renderer = C.vol
  type texture = C.vol
  type surface = C.vol
  type font = C.vol

  val gets = C.get_sym "/usr/lib/libSDL2.so"
  val gets_img = C.get_sym "/usr/lib/libSDL2_image.so"

  exception Img_Load of string

  fun img_load file =
    let
      val s = (C.call1 (gets_img "IMG_Load") C.STRING C.POINTER) file
    in
      if C.fromCint s <> 0 then
        s
      else
        raise Img_Load file
    end

  fun create_texture_from_surface r s =
    (C.call2 (gets "SDL_CreateTextureFromSurface") (C.POINTER, C.POINTER) C.POINTER) (r, s)

  fun free_surface s =
    (C.call1 (gets "SDL_FreeSurface") C.POINTER C.VOID) s
end

(* vim: se ai et: *)
