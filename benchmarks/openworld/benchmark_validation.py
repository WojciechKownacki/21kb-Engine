"""Validate the authored flyby against the camera actually used by the renderer."""
import math


def validate_camera_path(rows, case):
    if case not in ('city_dense_50k', 'mixed_stream_10k'):
        return
    for row in rows:
        frame = int(row['frame']) + 1
        if case == 'mixed_stream_10k':
            phase = frame % 1200
            z = -40 + min(phase, 1200 - phase) * 1.25
        else:
            z = -40 + frame * .75
        if int(row.get('render_camera_valid', '0')) != 1:
            raise RuntimeError(f'{case}: missing rendered camera at frame {frame - 1}')
        for axis, expected in (('x', 0), ('y', 12), ('z', z)):
            actual = float(row[f'render_camera_{axis}'])
            if not math.isfinite(actual) or abs(actual - expected) > .01:
                raise RuntimeError(f'{case}: rendered camera {axis}={actual}, expected {expected} '
                                   f'at frame {frame - 1}')
