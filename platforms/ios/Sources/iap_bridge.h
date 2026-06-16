#pragma once
// C bridge between C++ engine and the Swift IAP layer.
//
// Functions below with "ae_iap_*" prefix split into two directions:
//   - ae_iap_purchase / ae_iap_restore : implemented in Swift (@_cdecl),
//     called from C++ game code to trigger a purchase flow.
//   - ae_iap_on_* : implemented in C++ by the game, called from Swift
//     when the StoreKit result arrives on the main thread.
//
// Usage from C++:
//   #include "iap_bridge.h"
//   ae_iap_purchase("com.example.coins_100");
//
// Implement in your C++ game logic:
//   extern "C" void ae_iap_on_purchase_complete(const char* product_id) { ... }
//   extern "C" void ae_iap_on_restore_complete(void) { ... }
//   extern "C" void ae_iap_on_error(const char* product_id, const char* message) { ... }

#ifdef __cplusplus
extern "C" {
#endif

// ── C++ → Swift (implemented in IAPManager.swift) ────────────────────────────

void ae_iap_purchase(const char* product_id);
void ae_iap_restore(void);

// ── Swift → C++ (implement in your game code) ────────────────────────────────

void ae_iap_on_purchase_complete(const char* product_id);
void ae_iap_on_restore_complete(void);
void ae_iap_on_error(const char* product_id, const char* message);

#ifdef __cplusplus
}
#endif
