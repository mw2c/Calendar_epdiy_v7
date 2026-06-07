#!env python3

import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "components" / "epdiy2" / "src" / "waveforms" / "epdiy_ED047TC2.h"
OUTPUT = ROOT / "components" / "epdiy2" / "src" / "waveforms" / "epdiy_ED060KD1.h"

# Sampled from examples/grayscale_test/epdiy_ED047TC2.jpg.
# Crop: center 50% of x, 2%-98% of y, split into 16 horizontal bands.
PHOTO_LUMA = [
    68.0, 68.9, 79.8, 103.6, 120.1, 120.3, 125.2, 156.0,
    158.9, 167.8, 178.1, 198.1, 193.4, 201.4, 213.0, 221.1,
]

# The pure inverse map separates compressed levels, but it over-corrects this
# panel because phase mixing is not a linear grayscale interpolation. Keep the
# ED047TC2 vendor waveform shape dominant and apply only a smoothed correction.
TARGET_CORRECTION_STRENGTH = 0.00

# Source-column remapping tends to damage ED047TC2's transition compensation on
# reverse grayscale tests. Leave source columns in the vendor coordinate system
# and only remap target rows.
SOURCE_CORRECTION_STRENGTH = 0.00
CORRECTION_SMOOTH_PASSES = 1

# Starting from the un-remapped ED047TC2 rows is much more stable than
# phase-interpolating adjacent target rows. The previous target-wide pulse bias
# moved too many first-stage source states in the two-step grayscale test, so
# keep the ED047TC2 rows intact and only apply explicit transition patches.
BIAS_FRACTION_PER_LUMA = 0.000
BIAS_FRACTION_CAP = 0.18
BIAS_MIN_LUMA = 1.5
BIAS_TARGET_SCALE = [
    1.0, 1.0, 1.0, 1.0,
    1.0, 1.0, 2.0, 1.6,
    1.3, 1.0, 1.0, 0.0,
    0.0, 1.0, 1.0, 1.0,
]

# Fine-grained overrides for the reverse-gradient GC16 path used by the
# grayscale test. Each tuple is (target, source, direction, fraction).
TRANSITION_BIAS = [
    (1, 15, "lighten", 0.20),
    (2, 15, "lighten", 0.20),
    (5, 15, "lighten", 0.20),
    (6, 15, "lighten", 0.20),
    (7, 15, "darken", 0.35),
    (11, 15, "darken", 0.15),
    (12, 15, "lighten", 0.25),
    (13, 15, "lighten", 0.30),
    (3, 12, "darken", 0.15),
    (4, 11, "darken", 0.15),
    (6, 9, "darken", 0.15),
    (7, 8, "darken", 0.35),
    (8, 7, "darken", 0.25),
    (9, 6, "darken", 0.25),
    (10, 5, "darken", 0.12),
    (11, 4, "darken", 0.18),
]

# Stronger local corrections for transitions that remain visibly compressed or
# reversed on ED060KD1. Unlike TRANSITION_BIAS, these entries add pulses by
# turning selected non-matching phases into the requested direction.
TRANSITION_FORCE = [
]

# Explicit phase-level trims for MODE_GC16. The distributed fraction selector is
# useful for coarse changes, but some late waveform phases have a much stronger
# effect on ED060KD1. These entries avoid those late phases.
TRANSITION_PHASE_BIAS = [
    (2, 7, 15, "darken", [25]),
    (2, 7, 8, "darken", [27]),
]

TRANSITION_PHASE_SET = [
    (2, 7, 15, 1, [42]),
    (2, 7, 8, 1, [19, 20, 21]),
]


def isotonic_increasing(values):
    """Pool adjacent violators algorithm for a monotonic luma curve."""
    blocks = []
    for index, value in enumerate(values):
        blocks.append([float(value), 1, [index]])
        while len(blocks) >= 2 and blocks[-2][0] > blocks[-1][0]:
            right = blocks.pop()
            left = blocks.pop()
            count = left[1] + right[1]
            average = (left[0] * left[1] + right[0] * right[1]) / count
            blocks.append([average, count, left[2] + right[2]])

    result = [0.0] * len(values)
    for average, _count, indexes in blocks:
        for index in indexes:
            result[index] = average
    return result


def inverse_curve(curve, value):
    if value <= curve[0]:
        return 0.0
    if value >= curve[-1]:
        return float(len(curve) - 1)

    for index in range(len(curve) - 1):
        left = curve[index]
        right = curve[index + 1]
        if left <= value <= right:
            if right == left:
                return float(index)
            return index + (value - left) / (right - left)

    return float(len(curve) - 1)


def smooth_correction(values, passes):
    smoothed = list(values)
    for _pass in range(passes):
        next_values = list(smoothed)
        for index in range(1, len(values) - 1):
            next_values[index] = (
                smoothed[index - 1] + 2 * smoothed[index] + smoothed[index + 1]
            ) / 4
        next_values[0] = values[0]
        next_values[-1] = values[-1]
        smoothed = next_values
    return smoothed


def build_level_map(strength):
    monotonic = isotonic_increasing(PHOTO_LUMA)
    low = monotonic[0]
    high = monotonic[-1]
    desired = [low + (high - low) * index / 15 for index in range(16)]

    inverse_mapping = [inverse_curve(monotonic, value) for value in desired]
    correction = [inverse_mapping[index] - index for index in range(16)]
    correction = smooth_correction(correction, CORRECTION_SMOOTH_PASSES)
    mapping = [
        index + strength * correction[index]
        for index in range(16)
    ]
    mapping[0] = 0.0
    mapping[-1] = 15.0
    return monotonic, desired, mapping


def parse_waveform_array(text, mode, temp_suffix=11):
    pattern = (
        rf"const uint8_t epd_wp_ED047TC2_{mode}_{temp_suffix}_data"
        rf"\[(\d+)\]\[16\]\[4\]\s*=\s*(.*?);"
    )
    match = re.search(pattern, text, re.DOTALL)
    if match is None:
        raise ValueError(f"could not find ED047TC2 mode {mode}_{temp_suffix}")

    phases = int(match.group(1))
    values = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(2))]
    expected = phases * 16 * 4
    if len(values) != expected:
        raise ValueError(f"mode {mode}_{temp_suffix}: expected {expected} bytes, got {len(values)}")

    data = []
    cursor = 0
    for _phase in range(phases):
        phase = []
        for _target in range(16):
            phase.append(values[cursor:cursor + 4])
            cursor += 4
        data.append(phase)
    return data


def get_op(row, source):
    byte = row[source // 4]
    shift = 6 - 2 * (source % 4)
    return (byte >> shift) & 0x03


def set_op(row, source, op):
    shift = 6 - 2 * (source % 4)
    row[source // 4] |= (op & 0x03) << shift


def write_op(row, source, op):
    shift = 6 - 2 * (source % 4)
    mask = 0x03 << shift
    row[source // 4] = (row[source // 4] & ~mask) | ((op & 0x03) << shift)


def distributed_choice(coord, phase, phases, salt):
    low = int(math.floor(coord))
    high = int(math.ceil(coord))
    if low == high:
        return low

    fraction = coord - low
    # 37 is coprime with the ED047TC2 phase counts used here (15 and 57).
    rank = ((phase * 37) + salt) % phases
    return high if rank < fraction * phases else low


def remap_mode(source_data, target_map, source_map, mode):
    phases = len(source_data)
    remapped = []
    preserve_same_gray = mode == 5

    for phase_index in range(phases):
        phase = []
        for target in range(16):
            row = [0, 0, 0, 0]
            target_coord = target_map[target]
            for source in range(16):
                if preserve_same_gray and source == target:
                    op = 0
                else:
                    old_target = distributed_choice(
                        target_coord,
                        phase_index,
                        phases,
                        salt=target * 11 + source * 3 + 1,
                    )
                    old_source = distributed_choice(
                        source_map[source],
                        phase_index,
                        phases,
                        salt=source * 13 + target * 5 + 7,
                    )
                    op = get_op(source_data[phase_index][old_target], old_source)
                set_op(row, source, op)
            phase.append(row)
        remapped.append(phase)
    return remapped


def clone_mode(data):
    return [[list(row) for row in phase] for phase in data]


def candidate_rank(phase, source, target, phases):
    return ((phase * 37) + (source * 17) + (target * 29)) % (phases * 16)


def apply_target_bias(data, monotonic, desired, mode):
    biased = clone_mode(data)
    phases = len(biased)
    preserve_same_gray = mode == 5

    for target in range(1, 15):
        delta = desired[target] - monotonic[target]
        if abs(delta) < BIAS_MIN_LUMA:
            continue

        if delta > 0:
            # The ED047TC2 row is too dark for this target: remove darkening.
            match_op = 1
            new_op = 0
        else:
            # The ED047TC2 row is too light for this target: remove lightening.
            match_op = 2
            new_op = 0

        candidates = []
        for phase_index in range(phases):
            for source in range(16):
                if preserve_same_gray and source == target:
                    continue
                if get_op(biased[phase_index][target], source) == match_op:
                    candidates.append((phase_index, source))

        if not candidates:
            continue

        fraction = min(
            BIAS_FRACTION_CAP,
            abs(delta) * BIAS_FRACTION_PER_LUMA * BIAS_TARGET_SCALE[target],
        )
        count = int(round(len(candidates) * fraction))
        if count == 0:
            continue

        candidates.sort(key=lambda item: candidate_rank(item[0], item[1], target, phases))
        for phase_index, source in candidates[:count]:
            write_op(biased[phase_index][target], source, new_op)

    return biased


def apply_transition_bias(data, mode):
    biased = clone_mode(data)
    phases = len(biased)
    preserve_same_gray = mode == 5

    for target, source, direction, fraction in TRANSITION_BIAS:
        if preserve_same_gray and source == target:
            continue

        if direction == "darken":
            match_op = 2
            new_op = 0
        elif direction == "lighten":
            match_op = 1
            new_op = 0
        else:
            raise ValueError(f"unknown transition bias direction: {direction}")

        candidates = []
        for phase_index in range(phases):
            if get_op(biased[phase_index][target], source) == match_op:
                candidates.append(phase_index)

        count = int(round(len(candidates) * fraction))
        if count == 0:
            continue

        candidates.sort(key=lambda phase_index: candidate_rank(phase_index, source, target, phases))
        for phase_index in candidates[:count]:
            write_op(biased[phase_index][target], source, new_op)

    return biased


def apply_transition_phase_bias(data, mode):
    biased = clone_mode(data)
    phases = len(biased)
    preserve_same_gray = mode == 5

    for patch_mode, target, source, direction, phase_indexes in TRANSITION_PHASE_BIAS:
        if patch_mode != mode:
            continue
        if preserve_same_gray and source == target:
            continue

        if direction == "darken":
            match_op = 2
            new_op = 0
        elif direction == "lighten":
            match_op = 1
            new_op = 0
        else:
            raise ValueError(f"unknown transition phase bias direction: {direction}")

        for phase_index in phase_indexes:
            if phase_index < 0 or phase_index >= phases:
                continue
            if get_op(biased[phase_index][target], source) == match_op:
                write_op(biased[phase_index][target], source, new_op)

    return biased


def apply_transition_phase_set(data, mode):
    patched = clone_mode(data)
    phases = len(patched)
    preserve_same_gray = mode == 5

    for patch_mode, target, source, op, phase_indexes in TRANSITION_PHASE_SET:
        if patch_mode != mode:
            continue
        if preserve_same_gray and source == target:
            continue

        for phase_index in phase_indexes:
            if phase_index < 0 or phase_index >= phases:
                continue
            write_op(patched[phase_index][target], source, op)

    return patched


def apply_transition_force(data, mode):
    forced = clone_mode(data)
    phases = len(forced)
    preserve_same_gray = mode == 5

    for target, source, direction, fraction in TRANSITION_FORCE:
        if preserve_same_gray and source == target:
            continue

        if direction == "darken":
            forced_op = 1
        elif direction == "lighten":
            forced_op = 2
        else:
            raise ValueError(f"unknown transition force direction: {direction}")

        candidates = []
        for phase_index in range(phases):
            if get_op(forced[phase_index][target], source) != forced_op:
                candidates.append(phase_index)

        count = int(round(len(candidates) * fraction))
        if count == 0:
            continue

        candidates.sort(key=lambda phase_index: candidate_rank(phase_index, source, target, phases))
        for phase_index in candidates[:count]:
            write_op(forced[phase_index][target], source, forced_op)

    return forced


def format_array(name, data):
    lines = [f"const uint8_t {name}[{len(data)}][16][4] = {{"]
    for phase_index, phase in enumerate(data):
        lines.append("    {")
        for target_index, row in enumerate(phase):
            row_text = ",".join(f"0x{byte:02x}" for byte in row)
            suffix = "," if target_index < 15 else ""
            lines.append(f"        {{{row_text}}}{suffix}")
        suffix = "," if phase_index < len(data) - 1 else ""
        lines.append(f"    }}{suffix}")
    lines.append("};")
    return "\n".join(lines)


def format_mode(mode, data):
    base = f"epd_wp_epdiy_ED060KD1_{mode}_0"
    return "\n".join(
        [
            format_array(f"{base}_data", data),
            (
                f"const EpdWaveformPhases {base} = "
                f"{{ .phases = {len(data)}, .phase_times = NULL, "
                f".luts = (const uint8_t*)&{base}_data[0] }};"
            ),
            f"const EpdWaveformPhases* epd_wm_epdiy_ED060KD1_{mode}_ranges[1] = {{ &{base} }};",
            (
                f"const EpdWaveformMode epd_wm_epdiy_ED060KD1_{mode} = "
                f"{{ .type = {mode}, .temp_ranges = 1, "
                f".range_data = &epd_wm_epdiy_ED060KD1_{mode}_ranges[0] }};"
            ),
        ]
    )


def render_header(modes, monotonic, desired, target_map, source_map):
    target_map_text = ", ".join(f"{value:.3f}" for value in target_map)
    source_map_text = ", ".join(f"{value:.3f}" for value in source_map)
    luma_text = ", ".join(f"{value:.1f}" for value in monotonic)
    desired_text = ", ".join(f"{value:.1f}" for value in desired)

    parts = [
        "/*",
        " * Generated by scripts/remap_ed047tc2_to_ed060kd1.py.",
        " * Source LUT: epdiy_ED047TC2.h, temperature range suffix _11.",
        (
            f" * Target correction strength: {TARGET_CORRECTION_STRENGTH:.2f}, "
            f"source correction strength: {SOURCE_CORRECTION_STRENGTH:.2f}, "
            f"smooth passes: {CORRECTION_SMOOTH_PASSES}"
        ),
        (
            f" * Target pulse bias: {BIAS_FRACTION_PER_LUMA:.3f} per luma, "
            f"cap {BIAS_FRACTION_CAP:.2f}, min {BIAS_MIN_LUMA:.1f}"
        ),
        (
            " * Target pulse bias scale: "
            + ", ".join(f"{scale:.1f}" for scale in BIAS_TARGET_SCALE)
        ),
        (
            " * Transition bias: "
            + ", ".join(
                f"t{target}/s{source}/{direction}/{fraction:.2f}"
                for target, source, direction, fraction in TRANSITION_BIAS
            )
        ),
        (
            " * Transition force: "
            + ", ".join(
                f"t{target}/s{source}/{direction}/{fraction:.2f}"
                for target, source, direction, fraction in TRANSITION_FORCE
            )
        ),
        (
            " * Transition phase bias: "
            + ", ".join(
                f"m{mode}/t{target}/s{source}/{direction}/p{','.join(str(phase) for phase in phases)}"
                for mode, target, source, direction, phases in TRANSITION_PHASE_BIAS
            )
        ),
        (
            " * Transition phase set: "
            + ", ".join(
                f"m{mode}/t{target}/s{source}/op{op}/p{','.join(str(phase) for phase in phases)}"
                for mode, target, source, op, phases in TRANSITION_PHASE_SET
            )
        ),
        f" * Monotonic photo luma: {luma_text}",
        f" * Desired luma: {desired_text}",
        f" * Target level -> ED047TC2 coordinate: {target_map_text}",
        f" * Source level -> ED047TC2 coordinate: {source_map_text}",
        " */",
        "#include <stddef.h>",
        "",
    ]

    for mode in (1, 2, 5):
        parts.append(format_mode(mode, modes[mode]))
        parts.append("")

    parts.extend(
        [
            "const EpdWaveformTempInterval epdiy_ED060KD1_intervals[1] = { { .min = 20, .max = 30 } };",
            (
                "const EpdWaveformMode* epdiy_ED060KD1_modes[3] = { "
                "&epd_wm_epdiy_ED060KD1_1,&epd_wm_epdiy_ED060KD1_2,&epd_wm_epdiy_ED060KD1_5 };"
            ),
            (
                "const EpdWaveform epdiy_ED060KD1 = { .num_modes = 3, .num_temp_ranges = 1, "
                ".mode_data = &epdiy_ED060KD1_modes[0], .temp_intervals = &epdiy_ED060KD1_intervals[0] };"
            ),
            "",
        ]
    )
    return "\n".join(parts)


def main():
    text = SOURCE.read_text(encoding="utf-8")
    monotonic, desired, target_map = build_level_map(TARGET_CORRECTION_STRENGTH)
    _monotonic, _desired, source_map = build_level_map(SOURCE_CORRECTION_STRENGTH)

    source_modes = {
        1: parse_waveform_array(text, 1),
        2: parse_waveform_array(text, 2),
        5: parse_waveform_array(text, 5),
    }
    modes = {
        1: source_modes[1],
        2: apply_transition_force(
            apply_transition_phase_set(
                apply_transition_phase_bias(
                    apply_transition_bias(
                        apply_target_bias(
                            remap_mode(source_modes[2], target_map, source_map, 2),
                            monotonic,
                            desired,
                            2,
                        ),
                        2,
                    ),
                    2,
                ),
                2,
            ),
            2,
        ),
        5: apply_transition_force(
            apply_transition_phase_set(
                apply_transition_phase_bias(
                    apply_transition_bias(
                        apply_target_bias(
                            remap_mode(source_modes[5], target_map, source_map, 5),
                            monotonic,
                            desired,
                            5,
                        ),
                        5,
                    ),
                    5,
                ),
                5,
            ),
            5,
        ),
    }

    OUTPUT.write_text(
        render_header(modes, monotonic, desired, target_map, source_map),
        encoding="utf-8",
        newline="\n",
    )

    print("monotonic photo luma:", ", ".join(f"{value:.1f}" for value in monotonic))
    print("desired luma:", ", ".join(f"{value:.1f}" for value in desired))
    print("target level -> ED047TC2 coordinate:", ", ".join(f"{value:.3f}" for value in target_map))
    print("source level -> ED047TC2 coordinate:", ", ".join(f"{value:.3f}" for value in source_map))
    print(f"wrote {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
