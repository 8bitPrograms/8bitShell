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
    set(before "while(REFLOW && old_lineinfo && old_row >= 0 && old_lineinfo[old_row].continuation)")
    string(FIND "${screen}" "${before}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pinned libvterm screen changed; review the local reflow patch")
    endif()
    string(REPLACE "${before}"
        "while(REFLOW && old_lineinfo && old_row > 0 && old_lineinfo[old_row].continuation)"
        screen "${screen}")
    get_filename_component(directory "${output}" DIRECTORY)
    file(MAKE_DIRECTORY "${directory}")
    file(CONFIGURE OUTPUT "${output}" CONTENT "${screen}" @ONLY)
endfunction()
