#!/usr/bin/env bash
#
# This script is a wrapper to the other download backends.
# Its role is to ensure atomicity when saving downloaded files
# back to DOWNLOAD_DIR, and not clutter DOWNLOAD_DIR with partial,
# failed downloads.
#
# To avoid cluttering DOWNLOAD_DIR, we download to a trashable
# location, namely in BUILD_DIR.
# Then, we move the downloaded file to a temporary file in the
# same directory as the final output file.
# This allows us to finally atomically rename it to its final
# name.
# If anything goes wrong, we just remove all the temporaries
# created so far.
#
# Based on Buildroot's generic package infrastructure:
# https://git.busybox.net/buildroot/tree/support/download/dl-wrapper
#

# We want to catch any unexpected failure, and exit immediately.
set -e

export BR_BACKEND_DL_GETOPTS=":hc:d:o:n:N:H:ru:qf:e"

main() {
    local OPT OPTARG
    local backend output hfile recurse quiet
    local -a uris

    # Parse our options; anything after '--' is for the backend
    while getopts ":c:d:D:o:n:N:H:rf:u:q" OPT; do
        case "${OPT}" in
        c)  cset="${OPTARG}";;
        d)  dl_dir="${OPTARG}";;
        o)  output="${OPTARG}";;
        n)  raw_base_name="${OPTARG}";;
        N)  base_name="${OPTARG}";;
        H)  hfile="${OPTARG}";;
        r)  recurse="-r";;
        f)  filename="${OPTARG}";;
        u)  uris+=( "${OPTARG}" );;
        q)  quiet="-q";;
        :)  error "option '%s' expects a mandatory argument\n" "${OPTARG}";;
        \?) error "unknown option '%s'\n" "${OPTARG}";;
        esac
    done

    # Forget our options, and keep only those for the backend
    shift $((OPTIND-1))

    if [ -z "${output}" ]; then
        error "no output specified, use -o\n"
    fi

    [ -e "${output}" ] && exit 0

    # Look through all the uris that we were given to download the package
    # source
    download_and_check=0
    for uri in "${uris[@]}"; do
        backend_urlencode="${uri%%+*}"
        backend="${backend_urlencode%|*}"
        case "${backend}" in
            git|svn|cvs|bzr|file|scp|hg) ;;
            *) backend="wget" ;;
        esac
        uri=${uri#*+}

        urlencode=${backend_urlencode#*|}
        # urlencode must be "urlencode"
        [ "${urlencode}" != "urlencode" ] && urlencode=""

        # tmpd is a temporary directory in which backends may store
        # intermediate by-products of the download.
        # tmpf is the file in which the backends should put the downloaded
        # content.
        # tmpd is located in $(BUILD_DIR), so as not to clutter the (precious)
        # $(DOWNLOAD_DIR)
        # We let the backends create tmpf, so they are able to set whatever
        # permission bits they want (although we're only really interested in
        # the executable bit.)
        tmpd="$(mktemp -d "${BUILD_DIR}/.${output##*/}.XXXXXX")"
        tmpf="${tmpd}/output"

        # Helpers expect to run in a directory that is *really* trashable, so
        # they are free to create whatever files and/or sub-dirs they might need.
        # Doing the 'cd' here rather than in all backends is easier.
        cd "${tmpd}"

        # If the backend fails, we can just remove the content of the temporary
        # directory to remove all the cruft it may have left behind, and try
        # the next URI until it succeeds. Once out of URI to try, we need to
        # cleanup and exit.
        if ! "${OLDPWD}/util/download/${backend}" \
                $([ -n "${urlencode}" ] && printf %s '-e') \
                -c "${cset}" \
                -d "${dl_dir}" \
                -n "${raw_base_name}" \
                -N "${base_name}" \
                -f "${filename}" \
                -u "${uri}" \
                -o "${tmpf}" \
                ${quiet} ${recurse} -- "${@}"
        then
            # cd back to keep path coherence
            cd "${OLDPWD}"
            rm -rf "${tmpd}"
            continue
        fi

        # cd back to free the temp-dir, so we can remove it later
        cd "${OLDPWD}"
        download_and_check=1
        break
    done

    # We tried every URI possible, none seems to work or to check against the
    # available hash. *ABORT MISSION*
    if [ "${download_and_check}" -eq 0 ]; then
        rm -rf "${tmpd}"
        exit 1
    fi

    # tmp_output is in the same directory as the final output, so we can
    # later move it atomically.
    tmp_output="$(mktemp "${output}.XXXXXX")"

    # 'mktemp' creates files with 'go=-rwx', so the files are not accessible
    # to users other than the one doing the download (and root, of course).
    # This can be problematic when a shared DOWNLOAD_DIR is used by different
    # users (e.g. on a build server), where all users may write to the shared
    # location, since other users would not be allowed to read the files
    # another user downloaded.
    # So, we restore the 'go' access rights to a more sensible value, while
    # still abiding by the current user's umask. We must do that before the
    # final 'mv', so just do it now.
    # Some backends (cp and scp) may create executable files, so we need to
    # carry the executable bit if needed.
    [ -x "${tmpf}" ] && new_mode=755 || new_mode=644
    new_mode=$(printf "%04o" $((0${new_mode} & ~0$(umask))))
    chmod ${new_mode} "${tmp_output}"

    # We must *not* unlink tmp_output, otherwise there is a small window
    # during which another download process may create the same tmp_output
    # name (very, very unlikely; but not impossible.)
    # Using 'cp' is not reliable, since 'cp' may unlink the destination file
    # if it is unable to open it with O_WRONLY|O_TRUNC; see:
    #   http://pubs.opengroup.org/onlinepubs/9699919799/utilities/cp.html
    # Since the destination filesystem can be anything, it might not support
    # O_TRUNC, so 'cp' would unlink it first.
    # Use 'cat' and append-redirection '>>' to save to the final location,
    # since that is the only way we can be 100% sure of the behaviour.
    if ! cat "${tmpf}" >>"${tmp_output}"; then
        rm -rf "${tmpd}" "${tmp_output}"
        exit 1
    fi
    rm -rf "${tmpd}"

    # tmp_output and output are on the same filesystem, so POSIX guarantees
    # that 'mv' is atomic, because it then uses rename() that POSIX mandates
    # to be atomic, see:
    #   http://pubs.opengroup.org/onlinepubs/9699919799/functions/rename.html
    if ! mv -f "${tmp_output}" "${output}"; then
        rm -f "${tmp_output}"
        exit 1
    fi

    return 0
}

trace()  { local msg="${1}"; shift; printf "%s: ${msg}" "${my_name}" "${@}"; }
warn()   { trace "${@}" >&2; }
errorN() { local ret="${1}"; shift; warn "${@}"; exit ${ret}; }
error()  { errorN 1 "${@}"; }

my_name="${0##*/}"
main "${@}"
