package dmon

when ODIN_OS == "windows" {
    when ODIN_DEBUG == true {
        foreign import dmon_lib "dmond.lib"
    }
    else {
        foreign import dmon_lib "dmon.lib"
    }
} else when ODIN_OS == "darwin" {
    foreign import dmon_lib "dmon.a"
}

Watch_Flags :: enum i32 {
  Recursive = 0x1,
  Symlinks = 0x2,
  Out_Of_Scope_Links = 0x4, // TODO: Not implemented yet
  Ignore_Directories = 0x8, // TODO: Not implemented yet
}

Action :: enum i32 {
  Create,
  Delete,
  Modify,
  Move,
}

Watch_Event_Callback :: proc "c" (watch_id: Watch_Id, action: Action, rootdir: cstring, filepath: cstring, old_filepath: cstring, user_data: rawptr)

Watch_Id :: struct {
  id: u32,
}

@(default_calling_convention="c", link_prefix="dmon_")
foreign dmon_lib {
  init :: proc "c" () ---
  deinit :: proc "c" () ---
  watch :: proc "c" (rootdir: cstring, watch_cb: Watch_Event_Callback) -> Watch_Id ---
  unwatch :: proc(id: Watch_Id) ---
}
