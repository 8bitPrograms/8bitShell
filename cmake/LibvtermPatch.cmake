# Local defensive patch to SHA256-pinned libvterm 0.3.3. Do not edit the fetched
# source in place or suppress the compiler's uninitialized-use diagnostic.
function(bitshell_patch_vterm input output)
    file(READ "${input}" parser)
    function(replace_checked before after)
        string(FIND "${parser}" "${before}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Pinned libvterm parser changed; review the local security patch")
        endif()
        string(REPLACE "${before}" "${after}" parser "${parser}")
        set(parser "${parser}" PARENT_SCOPE)
    endfunction()
    replace_checked("const char *string_start;" "const char *string_start = NULL;")
    replace_checked("bytes + pos - string_start" "(string_start ? (size_t)(bytes + pos - string_start) : 0)")
    replace_checked(
        "vt->parser.v.csi.args[vt->parser.v.csi.argi] *= 10;"
        "/* Bound arithmetic before multiplication on both long widths. */\n        if(vt->parser.v.csi.args[vt->parser.v.csi.argi] > (1000000L - (c - '0')) / 10) {\n          ENTER_NORMAL_STATE();\n          break;\n        }\n        vt->parser.v.csi.args[vt->parser.v.csi.argi] *= 10;")
    replace_checked(
        "if(c == ';') {\n        vt->parser.v.csi.argi++;"
        "if(c == ';') {\n        /* Never index beyond the fixed CSI argument array. */\n        if(vt->parser.v.csi.argi >= CSI_ARGS_MAX - 1) {\n          ENTER_NORMAL_STATE();\n          break;\n        }\n        vt->parser.v.csi.argi++;")
    replace_checked(
        "else\n          vt->parser.v.osc.command *= 10;"
        "else {\n          if(vt->parser.v.osc.command > (1000000 - (c - '0')) / 10) {\n            ENTER_NORMAL_STATE();\n            break;\n          }\n          vt->parser.v.osc.command *= 10;\n        }")
    get_filename_component(directory "${output}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(CONFIGURE OUTPUT "${output}" CONTENT "${parser}" @ONLY)
endfunction()

# A wrapped logical line can start in scrollback. Reflow may only walk back to
# the first visible row, never to row -1 in the screen allocation.
function(bitshell_patch_vterm_screen input output)
    file(READ "${input}" screen)
    function(replace_checked before after)
        string(FIND "${screen}" "${before}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Pinned libvterm screen changed; review the local screen patch")
        endif()
        string(REPLACE "${before}" "${after}" screen "${screen}")
        set(screen "${screen}" PARENT_SCOPE)
    endfunction()
    replace_checked(
        "while(REFLOW && old_lineinfo && old_row >= 0 && old_lineinfo[old_row].continuation)"
        "while(REFLOW && old_lineinfo && old_row > 0 && old_lineinfo[old_row].continuation)")
    # Wide glyph bounds handling from Vim 8.2.1353: https://github.com/vim/vim/commit/8b89614e69b9b2330539d0482e44f4724053e780
    replace_checked(
        [=[    getcell(screen, pos.row, pos.col + col)->chars[0] = (uint32_t)-1;]=]
        [=[  {
    ScreenCell *onecell = getcell(screen, pos.row, pos.col + col);
    if (onecell == NULL)
      break;
    onecell->chars[0] = (uint32_t)-1;
  }]=])
    # Resize cursor handling from Vim 9.0.1049: https://github.com/vim/vim/commit/67578e5bcf7404d40d42876b738b72e68add1a3e
    replace_checked(
        [=[  /* We really expect the cursor position to be set by now */
  if(active && (new_cursor.row == -1 || new_cursor.col == -1)) {
    fprintf(stderr, "screen_resize failed to update cursor position\n");
    abort();
  }]=]
        [=[  /* We really expect the cursor position to be set by now */
  /* Unfortunately we do get here when "new_rows" is one.  We don't want
   * to crash, so until the above code is fixed let's just set the cursor. */
  if(active && (new_cursor.row == -1 || new_cursor.col == -1)) {
    /* fprintf(stderr, "screen_resize failed to update cursor position\n");
     * abort(); */
    if (new_cursor.row < 0)
      new_cursor.row = 0;
    if (new_cursor.col < 0)
      new_cursor.col = 0;
  }]=])
    get_filename_component(directory "${output}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(CONFIGURE OUTPUT "${output}" CONTENT "${screen}" @ONLY)
endfunction()

# REP with no preceding graphic character is ignored rather than repeating a
# zero-width glyph; cursor handling carried from Vim's libvterm.
function(bitshell_patch_vterm_state input output)
    file(READ "${input}" state)
    function(replace_checked before after)
        string(FIND "${state}" "${before}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Pinned libvterm state changed; review the local state patch")
        endif()
        string(REPLACE "${before}" "${after}" state "${state}")
        set(state "${state}" PARENT_SCOPE)
    endfunction()
    # REP cursor handling from Vim 9.2.0989: https://github.com/vim/vim/commit/785bb3e9b271eb377dbd4157ce281eac7f16a021
    replace_checked(
        [=[  case 0x62: { // REP - ECMA-48 8.3.103
    const int row_width = THISROWWIDTH(state);]=]
        [=[  case 0x62: { // REP - ECMA-48 8.3.103
    const int row_width = THISROWWIDTH(state);

    // ECMA-48 repeats the preceding graphic character; when none was
    // printed yet "combine_width" is zero and the loop below would never
    // advance the cursor.  Ignore the control then, like xterm does.
    if(state->combine_width < 1)
      break;
]=])
    get_filename_component(directory "${output}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(CONFIGURE OUTPUT "${output}" CONTENT "${state}" @ONLY)
endfunction()
