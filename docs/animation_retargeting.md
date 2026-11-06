# Retargeting animation clips

`kb_cli animator check --project Sample --controller /Game/Animation/Walk.kbanimcontroller --character /Game/Rig/Target.kbskeleton` checks automatic bone-role matching and reports unmatched animated bones. A skeletal mesh can also identify the target skeleton.

To save a reusable mapping and bake motion for a target rig:

```sh
kb_cli animator retarget --project Sample --clip /Game/Animation/Walk.kbanim --character /Game/Rig/Target.kbskeleton --name TargetWalk --save-profile Mapping.kbretarget
kb_cli animator retarget --project Sample --clip /Game/Animation/Run.kbanim --character /Game/Rig/Target.kbskeleton --name TargetRun --profile Mapping.kbretarget
```

The output is an ordinary `.kbanim` clip in `Assets/Animation` (override with `--folder`), usable by the Animator editor, controllers and cooker. Existing output clips are refused. The profile is an authoring input; the final clip carries the mapped motion and requires no runtime profile file.

Profile format version 1 begins with `21kb.retarget 1`, followed by source skeleton signature, target signature and target bone count. Each following integer identifies the source bone index for the corresponding target bone; `-1` leaves that target bone in its reference pose. Edit these entries to map rigs with unrelated names. Rig signatures cover authored hierarchy and rest/bind data: changing either rig invalidates the profile. Invalid indices, stale signatures and unsupported versions are rejected before writing a clip.

Mapped rotations preserve character-space changes relative to the rest poses. The target keeps its bone proportions; root/hips translation scales with rig height. Missing animated bones are reported, and fewer than half of the animated source bones mapping to the target is rejected. Automatic matching uses exact names, normalized roles and ordered chains. This does not solve arbitrary rig constraints or contact correction.

`kb_editor_animation_retarget_package` verifies headless Animator authoring, CLI profile round trips, rejected stale mappings, fixed-time D3D11 skinning before/after cooking, and source-independent package startup. The test renderer writes comparison images; shipped players retain their development-option restrictions.
