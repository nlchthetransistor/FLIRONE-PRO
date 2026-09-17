# Cau truc task va worker

Tai lieu nay la ma tran ownership cho active implementation trong `src/`. Moi task phai co
mot worker chinh, cac worker review neu co rui ro lien module, input/output ro rang va gate
kiem tra. `S0/S1` khong duoc coi la done chi vi build thanh cong.

## Luong tong quat

```text
Requirement -> ARCHITECT -> implementation worker -> QA_VALIDATION
                    |               |                    |
                    +--------> DOCS_RELEASE <------------+
```

## Ma tran giai doan

| Giai doan | Worker chinh | Input | Output mong doi | Gate |
|---|---|---|---|---|
| 0. Baseline | ARCHITECT | README, `src/Makefile`, source, config, git diff | Architecture map, assumptions, impacted files | Active path duoc xac dinh; backup khong bi sua nham |
| 1. Requirement/risk | ARCHITECT + THERMAL_CALIBRATION | Use case, patient workflow, reference standard | Requirement, hazard, acceptance criteria, uncertainty questions | Khong dung tu ngu diagnostic neu chua co evidence |
| 2. USB acquisition | HARDWARE_IO | FLIR VID/PID, endpoint, raw frame format | Frame contract, reconnect/timeout, health state | Test disconnect/reconnect; khong phat stale frame |
| 3. Environment sensing | HARDWARE_IO | DHT12 readings, I2C config | Ambient/RH sample + validity + age/provenance | Invalid/stale reading co status; test sensor failure |
| 4. Thermal conversion | THERMAL_CALIBRATION | Raw frame, ambient, tunables, Planck constants | `ProcessedFrame`, formula/LUT notes, uncertainty | Reference-object comparison; range/NaN checks |
| 5. Calibration study | THERMAL_CALIBRATION + QA_VALIDATION | Reference body/blackbody, multi-point Ta/RH, repeats, time | Calibration dataset, bias, repeatability, drift, limits | Protocol va evidence luu; tuning khong thay validation |
| 6. Runtime lifecycle | RUNTIME | Threads, locks, shutdown, queues, reconnect | Ownership/lifecycle decision, bounded behavior | Clean SIGTERM, no deadlock, bounded queue/retry |
| 7. Web contract | WEB_CONTRACT | JSON message types, UI actions, coordinate system | Versioned message schema, validation, UI states | Spot/ROI bounds, reconnect, error display |
| 8. Motion control | HARDWARE_IO | Servo config, angle mapping, fail state | Servo command contract, limits, safe startup/shutdown | Range test, no unsafe movement on invalid command |
| 9. Persistence/export | DATA_PRIVACY | Patient fields, screenshot, Sheets payload | Data classification, retention, retry/offline policy | No secret in repo/log; failed upload visible |
| 10. Verification | QA_VALIDATION | Build, tests, hardware and calibration artifacts | Test report with PASS/FAIL/NOT VERIFIED | S0/S1 closed or explicitly release-blocked |
| 11. Documentation/release | DOCS_RELEASE | Diff, worker reports, test evidence, config changes | Updated README/cautruc, migration/release note | Docs match runtime; rollback documented |

## Worker contract

Moi worker phai tra ve:

```text
ROLE:
SCOPE:
OBSERVATIONS: file + symbol + evidence
ASSUMPTIONS:
RISKS: severity + impact + containment
FILES_TO_CHANGE:
INPUTS_AND_OUTPUTS:
ACCEPTANCE_CRITERIA:
TEST_COMMANDS_OR_HARDWARE_STEPS:
RESULT: PASS | FAIL | NOT VERIFIED ON HARDWARE
UNRESOLVED:
```

## Contract runtime hien tai

| Contract | Producer | Consumer | Required fields |
|---|---|---|---|
| `ThermalFrame` | `FlirOneReader` | `process_frame` | 160x120 raw 16-bit, `valid`, frame freshness |
| `ProcessedFrame` | `process_frame` | Web/UI, spot, ROI, screenshot | RGB, per-pixel temp, min/max/center, raw extrema |
| Ambient sample | DHT12 thread | `main.cpp` thermal conversion | Ta, RH, `dht_ok`, freshness/quality |
| Frame WebSocket | `main.cpp`/`WebServer` | `src/web/app.js` | image, dimensions, temperatures, ambient, health, tunables |
| Measurement log | spot/ROI handler | Google Sheets logger | patient context, type, temp, tunables, Ta, RH, timestamp |
| Control command | web UI | `handle_client_message` | type, bounded coordinates/parameter/value, response/error |

## Definition of done

- Build pass: `make -C src clean && make -C src all`.
- Touched behavior has a focused test or a documented hardware/manual check.
- Measurement provenance and sensor health are preserved.
- Failure and recovery behavior are explicit.
- README and this file are updated for contract/architecture/config changes.
- No secrets or unnecessary patient-identifying data added.
- Calibration claims include method, reference, conditions, uncertainty and limits.
- Unverified hardware behavior is marked `NOT VERIFIED ON HARDWARE`.

## Open risks to track

- FLIR raw extraction and Planck constants need independent reference validation.
- Runtime LUT state is static and should be reviewed for thread safety before parallelizing.
- WebSocket broadcast currently has a single pending message path; frame backpressure/drop policy
  should be explicit.
- Patient name is used in screenshot filenames; sanitize, privacy and retention policy are needed.
- Google Sheets URL is configuration data and must be managed as a secret/credential-like value.
- Current systemd service runs as root; least privilege and USB/GPIO device permissions need review.
