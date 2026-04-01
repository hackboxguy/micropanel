#!/bin/sh
# video-info-jq.sh - Display key video parameters in a comparison table
# Usage: ./video-info-jq.sh file1.mp4 [file2.mp4] [file3.mp4] ...
# Dependencies: ffprobe, jq

set -e

usage() {
    echo "Usage: $(basename "$0") <file1> [file2] [file3] ..."
    echo "Display key video parameters in a comparison table."
    exit 1
}

[ $# -lt 1 ] && usage

command -v ffprobe >/dev/null 2>&1 || { echo "Error: ffprobe not found" >&2; exit 1; }
command -v jq      >/dev/null 2>&1 || { echo "Error: jq not found" >&2; exit 1; }

for f in "$@"; do
    [ -f "$f" ] || { echo "Error: file not found: $f" >&2; exit 1; }
done

# Extract info for one file, output pipe-delimited fields
extract_info() {
    _file="$1"
    _basename=$(basename "$_file")

    # Probe streams + format
    _sf_json=$(ffprobe -v quiet -print_format json -show_format -show_streams "$_file" 2>/dev/null)
    # Probe first frame for side data (HDR metadata)
    _fr_json=$(ffprobe -v quiet -print_format json -show_frames -read_intervals "%+#1" "$_file" 2>/dev/null)

    # Check video stream exists
    _has_video=$(printf '%s' "$_sf_json" | jq '[.streams[] | select(.codec_type=="video")] | length')
    [ "$_has_video" -lt 1 ] && { echo "Warning: no video stream in $_file" >&2; return 1; }

    # Video stream values
    _codec=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].codec_name // "?"')
    _profile=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].profile // ""')
    if [ -n "$_profile" ]; then
        _codec_str="${_codec} (${_profile})"
    else
        _codec_str="$_codec"
    fi

    _pix_fmt=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].pix_fmt // "?"')

    _bps=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].bits_per_raw_sample // ""')
    if [ -z "$_bps" ]; then
        case "$_pix_fmt" in
            *10*) _bps="10" ;;
            *12*) _bps="12" ;;
            *)    _bps="8" ;;
        esac
    fi
    _bit_depth="${_bps}-bit"

    _width=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].width // "?"')
    _height=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].height // "?"')
    _resolution="${_width}x${_height}"

    _rfps=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].r_frame_rate // "0/1"')
    _fps=$(echo "$_rfps" | awk -F/ '{if($2>0) printf "%.2f", $1/$2; else print "?"}')

    _primaries=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].color_primaries // "unknown"')
    _transfer=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].color_transfer // "unknown"')
    _matrix=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="video")][0].color_space // "unknown"')

    # HDR metadata from stream side_data_list and frame side_data_list
    _hdr_parts=""

    # Helper: search side_data_list in both stream and frame JSON
    _stream_sd=$(printf '%s' "$_sf_json" | jq -r '
        [.streams[] | select(.codec_type=="video")][0].side_data_list // []')
    _frame_sd=$(printf '%s' "$_fr_json" | jq -r '
        [.frames[0].side_data_list // []] | .[0] // []')

    # Mastering display - max luminance
    _max_lum=$(printf '%s\n%s' "$_stream_sd" "$_frame_sd" | jq -rs '
        [.[][] | select(.side_data_type == "Mastering display metadata")] |
        first.max_luminance // empty' 2>/dev/null || true)
    [ -n "$_max_lum" ] && _hdr_parts="MaxLum=${_max_lum}"

    # Content light level
    _max_cll=$(printf '%s\n%s' "$_stream_sd" "$_frame_sd" | jq -rs '
        [.[][] | select(.side_data_type == "Content light level metadata")] |
        first.max_content // empty' 2>/dev/null || true)
    _max_fall=$(printf '%s\n%s' "$_stream_sd" "$_frame_sd" | jq -rs '
        [.[][] | select(.side_data_type == "Content light level metadata")] |
        first.max_average // empty' 2>/dev/null || true)
    if [ -n "$_max_cll" ]; then
        [ -n "$_hdr_parts" ] && _hdr_parts="${_hdr_parts}, "
        _hdr_parts="${_hdr_parts}MaxCLL=${_max_cll}"
    fi
    if [ -n "$_max_fall" ]; then
        [ -n "$_hdr_parts" ] && _hdr_parts="${_hdr_parts}, "
        _hdr_parts="${_hdr_parts}MaxFALL=${_max_fall}"
    fi
    [ -z "$_hdr_parts" ] && _hdr_parts="None"

    # Bitrate
    _vbr=$(printf '%s' "$_sf_json" | jq -r '
        ([.streams[] | select(.codec_type=="video")][0].bit_rate // .format.bit_rate) // ""')
    if [ -n "$_vbr" ] && [ "$_vbr" != "null" ] && [ "$_vbr" != "" ]; then
        _vbr_str="$((_vbr / 1000)) kbps"
    else
        _vbr_str="?"
    fi

    # Duration
    _dur=$(printf '%s' "$_sf_json" | jq -r '
        ([.streams[] | select(.codec_type=="video")][0].duration //
         .format.duration) // ""')
    if [ -n "$_dur" ] && [ "$_dur" != "null" ]; then
        _dur_str=$(echo "$_dur" | awk '{m=int($1/60); s=$1-m*60; printf "%02d:%05.2f", m, s}')
    else
        _dur_str="?"
    fi

    # File size
    _size=$(printf '%s' "$_sf_json" | jq -r '.format.size // "0"')
    if [ "$_size" -ge 1073741824 ] 2>/dev/null; then
        _size_str=$(echo "$_size" | awk '{printf "%.2f GB", $1/1073741824}')
    elif [ "$_size" -ge 1048576 ] 2>/dev/null; then
        _size_str=$(echo "$_size" | awk '{printf "%.1f MB", $1/1048576}')
    elif [ "$_size" -ge 1024 ] 2>/dev/null; then
        _size_str=$(echo "$_size" | awk '{printf "%.1f KB", $1/1024}')
    else
        _size_str="${_size} B"
    fi

    # Audio
    _has_audio=$(printf '%s' "$_sf_json" | jq '[.streams[] | select(.codec_type=="audio")] | length')
    if [ "$_has_audio" -ge 1 ]; then
        _acodec=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="audio")][0].codec_name // "?"')
        _ach=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="audio")][0].channels // "?"')
        _asr=$(printf '%s' "$_sf_json" | jq -r '[.streams[] | select(.codec_type=="audio")][0].sample_rate // "?"')
        _audio_str="${_acodec}/${_ach}ch/${_asr}Hz"
    else
        _audio_str="None"
    fi

    # Output pipe-delimited
    printf '%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n' \
        "$_basename" "$_codec_str" "$_pix_fmt" "$_bit_depth" "$_resolution" \
        "$_fps" "$_primaries" "$_transfer" "$_matrix" "$_hdr_parts" \
        "$_vbr_str" "$_dur_str" "$_size_str" "$_audio_str"
}

# Collect data
HEADER="Filename|Codec|Pixel Format|Bit Depth|Resolution|FPS|Color Primaries|Transfer|Matrix|HDR Metadata|Bitrate|Duration|File Size|Audio"
ALL_DATA="$HEADER"

for f in "$@"; do
    _info=$(extract_info "$f") || continue
    ALL_DATA="${ALL_DATA}
${_info}"
done

# Print table using awk
printf '%s\n' "$ALL_DATA" | awk -F'|' '
BEGIN { num_fields = 0 }
{
    for (i = 1; i <= NF; i++) {
        data[NR][i] = $i
        len = length($i)
        if (NR == 1) {
            # First column is the label column
            if (i == 1 && len > label_w) label_w = len
        }
        if (i > 1) {
            # Track max width per data column (column index in output = i-1)
            ci = i - 1
            if (len > col_w[ci]) col_w[ci] = len
        }
    }
    num_fields = NF
    num_rows = NR
}
END {
    # For rows > 1, field 1 is label width, rest are values
    # But header row has field names as labels
    # Recalculate: label_w = max width of field 1 across all rows
    # col_w[c] = max width of field c+1 across all rows
    num_cols = num_fields - 1  # data columns (files)
    # Actually for header row, field 1 is "Filename" which is a label
    # and fields 2..N are file data — but wait, header has no file data
    # Rethink: row 1 is header labels, rows 2+ are file data
    # We need to transpose: labels become row headers, files become columns

    # Labels from row 1
    num_labels = num_fields
    # Number of files = num_rows - 1

    # Calculate label column width
    lw = 0
    for (i = 1; i <= num_labels; i++) {
        if (length(data[1][i]) > lw) lw = length(data[1][i])
    }

    # Calculate per-file column widths
    for (r = 2; r <= num_rows; r++) {
        cw[r-1] = 0
        for (i = 1; i <= num_labels; i++) {
            if (length(data[r][i]) > cw[r-1]) cw[r-1] = length(data[r][i])
        }
    }

    # Separator line
    sep = "+" sprintf("%-*s", lw + 2, "") "+"
    gsub(/ /, "-", sep)
    for (r = 2; r <= num_rows; r++) {
        chunk = sprintf("%-*s", cw[r-1] + 2, "") "+"
        gsub(/ /, "-", chunk)
        sep = sep chunk
    }

    print sep
    # Print rows: each label with corresponding values from each file
    for (i = 1; i <= num_labels; i++) {
        line = "| " sprintf("%-" lw "s", data[1][i]) " |"
        for (r = 2; r <= num_rows; r++) {
            line = line " " sprintf("%-" cw[r-1] "s", data[r][i]) " |"
        }
        print line
        if (i == 1) print sep  # separator after filename
    }
    print sep
}
'
