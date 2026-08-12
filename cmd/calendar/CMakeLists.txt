# calendar -- Unix v7 calendar(1), the reminder service.  Task C23 (../TODO.md);
# README.md beside this file is the account.
#
# TWO HALVES AND ONLY ONE OF THEM IS COMPILED.  /usr/lib/calendar is the C program below,
# which prints the egrep pattern for today and tomorrow; /usr/bin/calendar is the SHELL
# SCRIPT that runs it and greps with it, and is the second script on this image after
# ../lorder's.  Mode 0755 on the script and no `#!' line: ../../root.manifest says why.
#
# calendar.1.umm is already on the image -- B6_STAGE_MAN globs cmd/*/*.umm and
# ../../root.manifest has carried its stanza since before either half did.

# b6_obj reads both from the caller's scope.  b6cc has no -M, so the header dependency is
# the whole tree at once, as it is in lib/, kernel/, cmd/init/ and cmd/cron/.
set(KINC ${CMAKE_SOURCE_DIR}/include)
file(GLOB KHDRS ${KINC}/*.h ${KINC}/sys/*.h)

b6_prog(calendar DEST usr/lib/calendar SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/calendar.c)

# The script and the database, which are data and not C: copied, on the same footing as
# ../cron's /usr/lib/crontab and ../units's /usr/lib/units.  BUILD steps rather than
# configure_file(), so that `rm -rf build/rootfs' restores them.
add_custom_command(OUTPUT ${B6_ROOTFS}/usr/bin/calendar
    COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/calendar.sh ${B6_ROOTFS}/usr/bin/calendar
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/calendar.sh
    COMMENT "stage usr/bin/calendar" VERBATIM)

# ALL EIGHT OF v7's SUCCESSORS' FILES, not a selection: four of them cover 114 days of the
# year between them and eight cover 362, and a reminder service that is silent two days in
# three is a demonstration and not a service.  README.md has the measurement.
set(B6_CALENDARS
    calendar.birthday
    calendar.christian
    calendar.computer
    calendar.history
    calendar.holiday
    calendar.judaic
    calendar.music
    calendar.usholiday)

set(_staged_calendars ${B6_ROOTFS}/usr/bin/calendar)
foreach(_f ${B6_CALENDARS})
    add_custom_command(OUTPUT ${B6_ROOTFS}/usr/lib/calendars/${_f}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${B6_ROOTFS}/usr/lib/calendars
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/calendars/${_f}
                ${B6_ROOTFS}/usr/lib/calendars/${_f}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/calendars/${_f}
        COMMENT "stage usr/lib/calendars/${_f}" VERBATIM)
    list(APPEND _staged_calendars ${B6_ROOTFS}/usr/lib/calendars/${_f})
endforeach()

add_custom_target(rootfs_calendars ALL DEPENDS ${_staged_calendars})
add_dependencies(rootfs rootfs_calendars)

add_subdirectory(test)
