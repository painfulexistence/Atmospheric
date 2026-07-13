# Lua binding conventions

These files (`bind_*.cpp`) expose the engine's C++ API to Lua through
[sol2](https://github.com/ThePhD/sol2). The Lua surface is an intentional
*facade* — camelCase names, the `atmos.*` grouping, and some adapted
signatures — it is **not** a 1:1 mirror of the C++ headers.

The recurring maintenance pain is *drift*: the engine changes and a binding
silently no longer matches. The rule below is what keeps the bindings tracking
the engine "for free".

## Rule: bind member pointers, not forwarding lambdas

When a binding does nothing but call a single engine method/accessor with the
same arguments, **bind the member pointer directly** instead of wrapping it in
a lambda. sol2 introspects the pointer, so:

- the Lua signature **auto-tracks** the C++ one — add or reorder a parameter on
  the engine method and the binding follows with no edit here;
- a rename or removal becomes a **compile error on this exact line** instead of
  a runtime surprise. "Redo everything after an engine update" becomes "the
  compiler lists the handful of lines to fix".

### Instance methods (usertypes)

```cpp
// Prefer:
"setPerspective", &CameraComponent::SetPerspective,
"mass",           sol::property(&RigidbodyComponent::GetMass, &RigidbodyComponent::SetMass),

// Avoid when it only forwards:
"setPerspective", [](CameraComponent* c, float f, float a, float n, float fa) { c->SetPerspective(f, a, n, fa); },
```

### Free functions bound to a subsystem singleton

Use the 3-argument `set_function(name, &Type::Method, instance)` form — it binds
a member pointer against a fixed instance, so no argument list is restated:

```cpp
// Prefer:
audioTable.set_function("playSound", &AudioSubsystem::PlaySound, audio);
physics.set_function("setGravity", &Physics3DSubsystem::SetGravity, Physics3DSubsystem::Get());

// Avoid when it only forwards:
audioTable["playSound"] = [audio](SoundID id) { audio->PlaySound(id); };
```

## When a lambda IS the right call

Keep a lambda whenever the binding genuinely *adapts* rather than forwards —
these can't be expressed as a bare member pointer, and that's fine:

- **Type adaptation** — building a `LightProps`/`CameraProps` from a `sol::table`,
  returning a `std::tuple` (e.g. `getScreenSize`), constructing a result table
  (e.g. `raycast`).
- **Enum bridging** — `static_cast<Key>(intFromLua)` in the input bindings.
- **Overloaded targets** — `&Type::Method` is ambiguous; use
  `sol::resolve<Sig>(&Type::Method)` or `sol::overload(...)`.
- **Extra logic/state** — the immediate-mode drawing helpers that thread the
  cached draw color.

These still fail to compile if the underlying method's name goes away, so they
don't drift silently either — but prefer the member-pointer form wherever a
binding is a pure pass-through.
