# 27. Assistant (M7) — implementation plan

Plan for the Online Assistant milestone. It maps the existing device-facing
contract ([`architecture/cloud_api_contract.md`](architecture/cloud_api_contract.md))
and the milestone acceptance ([`08_project_milestones.md`](08_project_milestones.md))
onto concrete modules, and separates what can be built and tested **now, without
any cloud backend or secret keys** from what must wait.

## Goal and scope

Deliver the **text-query** path end to end:

1. In Assistant Mode the user picks a preset query (encoder scrolls, touch
   submits).
2. The device POSTs `/v1/query` and shows a compact **result card**.
3. The answer's `mood` drives the procedural face through the Behavior Engine,
   so CoPet visibly reacts to what it "says".

Voice input (`/v1/audio-query`, mic recording/upload/STT) and the Wi-Fi SoftAP
setup page are explicitly **out of scope for M7** and handled later.

## Principles

- **No secrets in firmware or Git.** The backend holds all third-party keys; the
  device only knows an endpoint URL (Kconfig). The first prototype may call one
  public endpoint without a key.
- **Never block the Desk UI.** The assistant runs in a background task and
  publishes a snapshot, mirroring `weather_service`.
- **Pure logic is host-tested.** The sub-state machine, preset table and
  `mood → emotion` mapping have no ESP-IDF dependency and get host tests, like
  the other modes.
- **One active request per device** (per the contract).

## Modules

Mirrors the existing `modes/` + `ui/` + `services/` split.

| Module | Role | Testable on host |
| --- | --- | --- |
| `modes/assistant_mode.{c,h}` | Sub-state machine + preset selection + request lifecycle (timeout, result, error). No I/O. | Yes |
| `ui/assistant_ui.{c,h}` | Result card renderer on `ui_canvas` (question, wrapped answer, status/error line). | Yes (pure draw) |
| `services/assistant_service.{c,h}` | Background task: submit a query, fetch, publish a snapshot. Pluggable backend. | Backend-dependent |

### Sub-states (from the spec)

```text
ASSISTANT_IDLE      -> pick a preset
ASSISTANT_WAITING   -> request in flight (spinner + timeout)
ASSISTANT_RESULT    -> show the answer card (until back / ttl)
ASSISTANT_ERROR     -> show the error text, recoverable (back -> IDLE)
ASSISTANT_RECORDING -> (deferred, audio path)
ASSISTANT_UPLOADING -> (deferred, audio path)
```

Transitions: `IDLE --submit--> WAITING`; `WAITING --answer--> RESULT`;
`WAITING --error/timeout--> ERROR`; `RESULT/ERROR --back--> IDLE`. Input while
`WAITING` is ignored (single in-flight request).

### Service snapshot (mirror `weather_service`)

```c
typedef enum {
    ASSISTANT_SERVICE_IDLE,
    ASSISTANT_SERVICE_WAITING_WIFI,
    ASSISTANT_SERVICE_FETCHING,
    ASSISTANT_SERVICE_READY,
    ASSISTANT_SERVICE_ERROR,
} assistant_service_status_t;

typedef struct {
    assistant_service_status_t status;
    bool     has_answer;
    char     text[192];     /* answer or error text, UTF-8, truncated to card */
    char     mood[16];      /* e.g. "helpful"; mapped to a face emotion */
    uint32_t ttl_sec;
    uint32_t request_id;
    uint32_t revision;      /* bumped on each new snapshot, like weather */
} assistant_service_snapshot_t;

esp_err_t assistant_service_start(void);
esp_err_t assistant_service_submit(int preset_index); /* one in flight */
void      assistant_service_get_snapshot(assistant_service_snapshot_t *out);
```

### Pluggable backend

The service calls a backend interface so the whole flow works before any real
Cloud API exists:

- **stub backend (default, builds now):** returns a canned answer + mood after a
  short simulated delay. Lets us exercise Assistant Mode, the card and the face
  reaction on real hardware with **no network**.
- **http backend:** HTTPS `POST {endpoint}/v1/query` with the contract's request
  body, parse the JSON response (`status`, `text`, `mood`, `ttl_sec` / `error`).
  Endpoint URL from Kconfig; no key. Reuses the TLS/HTTP client already used by
  `weather_service`.

Selected by Kconfig (`stub` | `http`).

## What we build now (no cloud, no keys)

1. `assistant_mode` sub-state machine + preset table (`weather`, `time`, one
   fixed dev question), `locale = "ru-RU"`. **+ host tests.**
2. `assistant_ui` result card (word-wrap `text`, show question + status).
   **+ host tests** for wrapping/layout helpers.
3. `assistant_service` with the **stub backend** + Kconfig scaffolding; wire
   Assistant into the Menu and poll the snapshot in the main loop (like weather).
   → Full flow visible on device.
4. `mood → emotion` mapping table (answer `mood` → a `copet_behavior_id_t`),
   posted to the Behavior Engine so the face reacts. **+ host test.**
5. `http` backend behind Kconfig, parsing `/v1/query`. Can be pointed at any
   compatible endpoint (a local mock or a simple public one) for a real request.

Each step is independently buildable, testable and flashable.

## Deferred (needs cloud / keys / more hardware)

- Deploying the actual CoPet Cloud `/v1/query` backend adapter.
- `/v1/audio-query`: mic recording (`ASSISTANT_RECORDING`), chunked upload
  (`ASSISTANT_UPLOADING`), speech-to-text. Depends on the text path being stable.
- Wi-Fi SoftAP `CoPet-Setup` provisioning page (today Wi-Fi is Kconfig-provided).
- Answer caching / `ttl_sec` reuse.

## Testing

- **Host:** `assistant_mode` transitions (submit → waiting → result; timeout →
  error; back → idle; ignore input while waiting), `mood → emotion` mapping, card
  text wrapping. Keeps the "pure logic is host-tested" discipline.
- **Device:** stub backend proves flow + card + face; http backend against a mock
  or public endpoint proves the real request/parse.

## Milestone acceptance (docs 08, M7)

| Acceptance criterion | Covered by |
| --- | --- |
| preset text query reaches the Cloud API | http backend + preset table |
| compact result card is displayed | `assistant_ui` |
| errors/timeouts are visible and recoverable | `ASSISTANT_ERROR` + card + back-to-idle |
| short INMP441 upload after the text path is stable | deferred `/v1/audio-query` |

## Suggested build order

1. `assistant_mode` + host tests.
2. `assistant_ui` card + host tests.
3. `assistant_service` (stub) + Kconfig + Menu/main-loop wiring → on-device demo.
4. `mood → emotion` mapping into the Behavior Engine.
5. `http` backend (Kconfig) + JSON parse → a real `/v1/query`.
6. (later milestone) `/v1/audio-query` voice path.
