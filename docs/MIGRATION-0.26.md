# Moving an adapter to SDK 0.26.0

0.26.0 is the release that stops rebuilding every adapter for every SDK
change (README, "The binary boundary"). The price is paid once, here: the
public data types are classes behind a d-pointer, and code that wrote fields
now calls accessors. Nothing on the wire changed; core is indifferent.

## The rules, in one place

- A getter carries the field's name: `channel.kind()`, `request.cmdId()`.
- A setter is `set` plus the name and returns the object, so calls chain:
  `channel.setName("Power").setKind(v1::ChannelKind::Power)`.
- A nested part has a const reader and a mutable one, written in place:
  `field.layout().setCells(2)`, `schema.factory().addField(f)`,
  `call.tls().setEnabled(true)`.
- A list has a reader, a setter and an `add`: `room.deviceExternalIds()`,
  `room.setDeviceExternalIds({...})`, `room.addDeviceExternalId("d1")`.
- Small building blocks keep short constructors, so a list of them still
  reads as braces: `field.setOptions({{"a", "A"}, {"b"}})`,
  `response.setFormValues({{"host", v1::ScalarValue{...}}})`.
- Copies are deep. A moved-from object reads as empty and is valid.
- The requests core sends are read-only for an adapter: getters only.

## Entities (phi/adapter/v1)

| Before | After |
|---|---|
| `v1::Channel c; c.externalId = "p"; c.name = "Power"; c.kind = K; c.dataType = T; c.flags = F;` | `v1::Channel c("p", "Power", K, T, F);` |
| `v1::Channel c; c.name = "Power"; c.kind = K;` | `v1::Channel c; c.setName("Power").setKind(K);` |
| `d.flags \|= Battery;` `d.mediaPlayerFeatures \|= Play;` | `d.addFlags(Battery);` `d.addMediaPlayerFeatures(Play);` (Channel and Scene have `addFlags` too) |
| `c.minValue = 0; c.maxValue = 100; c.stepValue = 1;` | `c.setRange(0, 100, 1);` |
| `c.lastValue = v; c.lastUpdateMs = t; c.hasValue = true;` | `c.setLastValue(v, t);` |
| `c.hasValue = false;` | `c.clearValue();` |
| `c.choices.push_back({"a", "A"});` | `c.addChoice("a", "A");` |
| `d.effects.push_back(effect);` | `d.addEffect(effect);` |
| `effect.effect = E; effect.id = "x";` | `effect.setEffect(E).setId("x");` |
| `room.deviceExternalIds = ids;` | `room.setDeviceExternalIds(ids);` |
| `scene.scopeType = "group"; scene.scopeExternalId = g;` | `scene.setScope("group", g);` |
| reading `c.externalId`, `d.deviceClass`, ... | `c.externalId()`, `d.deviceClass()`, ... |

`Device`, `Channel`, `Room`, `Group`, `Scene`, `DeviceEffectDescriptor`:
every former field is a getter/setter pair of the same name.

## The record, forms, actions (schema.h) and answers (response.h)

| Before | After |
|---|---|
| `config().adapter.externalId` | `config().adapter().externalId()` |
| `v1::AdapterConfigField f; f.key = "k"; f.type = T; f.label = "L"; f.description = "D";` | `v1::AdapterConfigField f("k", T, "L", "D");` (the description is optional) |
| `f.layout.cells = 2; f.layout.newRow = true;` | `f.layout().setCells(2).setNewRow(true);` |
| `f.visibility = {...}` / `f.visibility.fieldKey = "tls"` | `f.setVisibility({"tls", true})` / `f.visibility().setFieldKey("tls")` |
| `f.options = {{"a","A"}};` `f.actions.push_back({"probe","Probe"});` | `f.setOptions({{"a","A"}});` `f.addAction("probe", "Probe");` |
| `f.minValue = 1.5;` `f.minValue.reset();` | `f.setMinValue(1.5);` `f.setMinValue(std::nullopt);` |
| `schema.factory.title = "Bridge"; schema.factory.fields.push_back(f);` | `schema.factory().setTitle("Bridge").addField(f);` |
| `schema.factory.layout.columns = 2;` | `schema.factory().layout().setColumns(2);` |
| `v1::AdapterActionDescriptor a; a.id = "probe"; a.label = "Probe";` | `v1::AdapterActionDescriptor a("probe", "Probe");` |
| `a.hasForm = true; a.formLayout.width = Narrow;` | `a.setHasForm(true); a.formLayout().setWidth(Narrow);` |
| `a.confirm.title = t; a.confirm.message = m;` | `a.setConfirm({t, m, okLabel, cancelLabel});` |
| `caps.required = R; caps.factoryActions.push_back(a);` | `caps.setRequired(R).addFactoryAction(a);` |
| `AdapterDescriptor d; d.pluginType = ...; d.configSchema = schema;` | `d.setPluginType(...).setConfigSchema(schema);` |
| `descriptor.capabilities.flags = F;` | `descriptor.capabilities().setFlags(F);` |

Answers:

| Before | After |
|---|---|
| `CmdResponse r; r.id = id; r.status = Success; r.finalValue = v;` | `auto r = CmdResponse::success(id, v);` |
| `r.id = id; r.status = S; r.error = "..."; r.errorParams = {...};` | `auto r = CmdResponse::failure(id, S, "...", {...});` |
| a helper answering with a runtime status, success or not | `CmdResponse::answer(id, status, error)` / `ActionResponse::answer(id, status, error)` |
| `ActionResponse r; r.id = id; r.status = Success;` | `auto r = ActionResponse::success(id);` |
| `r.resultType = String; r.resultValue = v;` | `r.setResult(v, ActionResultType::String);` |
| `r.resultType = Data; r.dataJson = json;` | `r.setData(json);` |
| `r.resultType = Display; r.display.text = t; r.display.code = c;` | `r.setDisplay({t, c});` |
| `r.resultType = Run; r.run.runId = ...;` | `AdapterRunHandle run; run.setRunId(...); r.setRun(run);` |
| `r.formValues.push_back({"k", data});` | `r.addFormValue("k", data);` |
| `r.fieldChoices.push_back({"k", options});` | `r.addFieldChoices("k", options);` |
| `r.reloadLayout = true; r.tsMs = 0;` | `r.setReloadLayout(true);` (tsMs 0 is the default: the library stamps it) |

`setDisplay`, `setRun` and `setData` set `resultType` themselves.

## The requests core sends (phi/adapter/sdk/requests.h)

Getters only, same names: `request.cmdId()`, `request.externalId()`,
`request.deviceExternalId()`, `request.channelExternalId()`,
`request.value()`, `request.hasScalarValue()`, `request.valueJson()`,
`request.actionId()`, `request.paramsJson()`, `request.adapter()` (the
record, then `.externalId()`, `.host()`, `.user()`, `.password()`, ...),
`request.staticConfigJson()`, `request.mediaId()`, and so on.

## HTTP (phi/adapter/net)

| Before | After |
|---|---|
| `HttpClient::Call call; call.url = u; call.method = "PUT";` | `HttpClient::Call call(u, "PUT");` |
| `call.headers.push_back({"Accept", "application/json"});` | `call.addHeader("Accept", "application/json");` |
| finding and overwriting one header in `call.headers` | `call.setHeader("Accept", "text/event-stream");` (replaces or adds) |
| `call.body = b; call.headers.push_back({"Content-Type", ct});` | `call.setBody(b, ct);` |
| `call.credentials = {user, pass};` | `call.setCredentials({user, pass});` |
| `call.timeout = 2s;` `call.tls = settings;` `call.tlsServerName = n;` | `call.setTimeout(2s);` `call.setTls(settings);` `call.setTlsServerName(n);` |
| `call.tls.enabled = true;` | `call.tls().setEnabled(true);` |
| `result.ok`, `result.status`, `result.body`, `result.error` | `result.ok()`, `result.status()`, `result.body()`, `result.error()` |
| looping `result.headers` for one name | `result.header("Content-Type")` |
| `std::move(result.body)` | `result.takeBody()` |
| `HttpClient::StreamHandlers h; h.head = ...; h.chunk = ...; h.done = ...;` | `HttpClient::StreamHandlers h(head, chunk, done);` or `h.setHead(...)...` |
| `net::Fetch f; f.url = u;` `fetch(f).body` | `net::Fetch f(u);` `fetch(f).body()` |
| `v1::TlsSettings t; t.enabled = true; t.caFile = path;` | `t.setEnabled(true).setCaFile(path);` |
| `#include "phi/adapter/net/http_message.h"` / `http_auth.h` | gone from the public headers; `Header` and `Credentials` are in `http_types.h` |

## The main loop

| Before | After |
|---|---|
| `SidecarMainOptions o; o.pollTimeout = 50ms; o.onIteration = [&]{...};` | `o.setPollTimeout(50ms).setOnIteration([&]{...});` |

## What did not change

Enums and flags (`types.h`), `ScalarValue`, `Color`, `ChannelValueFields`,
`FrameHeader`, the `AdapterFactory`/`AdapterInstance` virtuals an adapter
overrides, `runSidecarMain`, the logging helpers, the wire.

## Checking a port

Build with `-Werror=deprecated` off and the usual warnings on; the compiler
finds every former field access ("invalid use of member function ... did you
forget the ()?"). Then run the adapter against a core and watch its first
report: the golden fixtures prove the library emits the same bytes, so a
difference is the adapter's.
