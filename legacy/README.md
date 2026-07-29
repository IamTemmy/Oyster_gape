# Legacy MBRACE code (reference only)

Archived snapshot of prior MBRACE / oyster-gape work by an earlier researcher,
kept here for context and reference only. It is NOT the active codebase — all
current firmware and analysis in this project are original work and live
elsewhere in this repo. Nothing in legacy/ is maintained; do not flash or deploy
it as-is.

## Credit
All code in this folder was written by James Curtis Addy
(GitHub: @jamesaddy789; commits authored as "Curtis" and "jamesaddy789").
Included with the permission of the project supervisor as continuation of the
JSU MBRACE project. Original authorship and credit remain with the author.
The source repositories carried no license file; this snapshot is retained for
internal research continuity with permission.

## Provenance (captured 2026-07-29)
- biosensor-firmware/ <- jamesaddy789/Biosensor-Code-JSU-Mbrace  (c3a0c95, 2021-05-24)
- matlab-analysis/    <- jamesaddy789/mbrace-oyster-matlab       (e8dafe5, 2020-10-31)
- python-tools/       <- jamesaddy789/Mbrace-Python              (8f42db7, 2020-01-09)
- android-app/        <- jamesaddy789/Mbrace-Oyster-Graph-Android-App (a907de5, 2019-09-13)

## What each part is
- biosensor-firmware/ : old Readers (Nano sensor sketches) + Senders (ESP/MKR SD+telemetry), I2C-linked. Source of the verified 6-channel Nano pin map.
- matlab-analysis/    : FFT / spawning-detection analysis; includes a real 2018 field dataset (.bin).
- python-tools/       : server-side + helper scripts that ingested the posted data blocks.
- android-app/        : Android app that graphed oyster gape (the prior live dashboard).
