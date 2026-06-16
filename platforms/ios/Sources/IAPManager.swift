import StoreKit

// StoreKit 2 IAP manager.
//
// StoreKit 2 is Swift-only by design: Product, Transaction, and AppStore
// are all Swift concurrency types with no Objective-C equivalents.
// This is the primary reason the iOS shell is Swift rather than Obj-C++.
//
// C++ game code calls ae_iap_purchase / ae_iap_restore (declared in
// iap_bridge.h, exported via @_cdecl below).  Results are delivered back
// to C++ via ae_iap_on_purchase_complete / ae_iap_on_error, which the
// game must implement.

@MainActor
final class IAPManager {

    static let shared = IAPManager()
    private var updateListener: Task<Void, Never>?

    private init() {}

    // MARK: - Transaction listener

    // Call once at app launch (before the game loop starts).
    // Handles transactions delivered while the app was closed — e.g. a
    // subscription renewal or a purchase approved by Ask to Buy.
    func listenForTransactions() async {
        updateListener = Task.detached(priority: .background) {
            for await result in Transaction.updates {
                await self.deliver(result)
            }
        }
    }

    // MARK: - Purchase

    func purchase(productId: String) async {
        do {
            let products = try await Product.products(for: [productId])
            guard let product = products.first else {
                ae_iap_on_error(productId, "Product not found in App Store")
                return
            }
            let result = try await product.purchase()
            switch result {
            case .success(let verification):
                let transaction = try verification.payloadValue
                await transaction.finish()
                ae_iap_on_purchase_complete(transaction.productID)
            case .userCancelled:
                break
            case .pending:
                // Ask to Buy or payment processing — result arrives via listener
                break
            @unknown default:
                break
            }
        } catch {
            ae_iap_on_error(productId, error.localizedDescription)
        }
    }

    // MARK: - Restore

    func restore() async {
        do {
            // AppStore.sync() re-validates and re-delivers all current
            // entitlements.  Use this for "Restore Purchases" buttons.
            try await AppStore.sync()
            ae_iap_on_restore_complete()
        } catch {
            ae_iap_on_error("", error.localizedDescription)
        }
    }

    // MARK: - Internal

    private func deliver(_ result: VerificationResult<Transaction>) async {
        guard case .verified(let transaction) = result else { return }
        ae_iap_on_purchase_complete(transaction.productID)
        await transaction.finish()
    }

    deinit {
        updateListener?.cancel()
    }
}

// MARK: - C bridge (called from C++ via iap_bridge.h)

@_cdecl("ae_iap_purchase")
func ae_iap_purchase_c(productId: UnsafePointer<CChar>) {
    let id = String(cString: productId)
    Task { @MainActor in
        await IAPManager.shared.purchase(productId: id)
    }
}

@_cdecl("ae_iap_restore")
func ae_iap_restore_c() {
    Task { @MainActor in
        await IAPManager.shared.restore()
    }
}
