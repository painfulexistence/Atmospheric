package com.atmospheric.engine

import android.app.Activity
import android.util.Log
import com.android.billingclient.api.*
import kotlinx.coroutines.*

// Google Play Billing 7 IAP manager.
//
// Play Billing's Kotlin API uses suspend functions and Flow, which is why
// Kotlin is meaningfully better than Java here — the Java equivalent
// requires deeply nested callbacks (BillingFlowParams, PurchasesUpdatedListener,
// etc.) that Kotlin coroutines collapse into sequential-looking code.
//
// C++ calls into this class via JNI through MainActivity.nativePurchase /
// nativeRestorePurchases.  Results flow back via MainActivity.nativeOn* JNI
// calls (declared external in MainActivity.kt).

private const val TAG = "IAPManager"

class IAPManager(private val activity: MainActivity) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val purchasesUpdatedListener = PurchasesUpdatedListener { billingResult, purchases ->
        if (billingResult.responseCode == BillingClient.BillingResponseCode.OK && purchases != null) {
            scope.launch { purchases.forEach { handlePurchase(it) } }
        } else if (billingResult.responseCode != BillingClient.BillingResponseCode.USER_CANCELED) {
            activity.nativeOnPurchaseError(
                "",
                "Billing error: ${billingResult.debugMessage}"
            )
        }
    }

    private val billingClient: BillingClient = BillingClient.newBuilder(activity)
        .setListener(purchasesUpdatedListener)
        .enablePendingPurchases(
            PendingPurchasesParams.newBuilder().enableOneTimeProducts().build()
        )
        .build()

    // ── Connection ───────────────────────────────────────────────────────────

    fun connect() {
        billingClient.startConnection(object : BillingClientStateListener {
            override fun onBillingSetupFinished(result: BillingResult) {
                if (result.responseCode == BillingClient.BillingResponseCode.OK) {
                    // Re-deliver any purchases that completed while the app was closed.
                    scope.launch { deliverPendingPurchases() }
                }
            }
            override fun onBillingServiceDisconnected() {
                // BillingClient retries automatically; no action needed.
            }
        })
    }

    fun disconnect() {
        billingClient.endConnection()
        scope.cancel()
    }

    // ── Purchase ─────────────────────────────────────────────────────────────

    fun purchase(productId: String) {
        scope.launch {
            val productDetails = queryProduct(productId) ?: run {
                activity.nativeOnPurchaseError(productId, "Product not found in Play Store")
                return@launch
            }
            val params = BillingFlowParams.newBuilder()
                .setProductDetailsParamsList(
                    listOf(
                        BillingFlowParams.ProductDetailsParams.newBuilder()
                            .setProductDetails(productDetails)
                            .build()
                    )
                )
                .build()
            billingClient.launchBillingFlow(activity, params)
            // Result arrives in purchasesUpdatedListener
        }
    }

    // ── Restore ──────────────────────────────────────────────────────────────

    fun restore() {
        scope.launch {
            deliverPendingPurchases()
            activity.nativeOnRestoreComplete()
        }
    }

    // ── Internal ─────────────────────────────────────────────────────────────

    private suspend fun queryProduct(productId: String): ProductDetails? {
        val params = QueryProductDetailsParams.newBuilder()
            .setProductList(
                listOf(
                    QueryProductDetailsParams.Product.newBuilder()
                        .setProductId(productId)
                        .setProductType(BillingClient.ProductType.INAPP)
                        .build()
                )
            )
            .build()

        val result = billingClient.queryProductDetails(params)
        if (result.billingResult.responseCode != BillingClient.BillingResponseCode.OK) {
            Log.e(TAG, "queryProductDetails failed: ${result.billingResult.debugMessage}")
            return null
        }
        return result.productDetailsList?.firstOrNull()
    }

    private suspend fun deliverPendingPurchases() {
        val result = billingClient.queryPurchasesAsync(
            QueryPurchasesParams.newBuilder()
                .setProductType(BillingClient.ProductType.INAPP)
                .build()
        )
        if (result.billingResult.responseCode == BillingClient.BillingResponseCode.OK) {
            result.purchasesList.forEach { handlePurchase(it) }
        }
    }

    private suspend fun handlePurchase(purchase: Purchase) {
        if (purchase.purchaseState != Purchase.PurchaseState.PURCHASED) return

        // Acknowledge to avoid automatic refund after 3 days
        if (!purchase.isAcknowledged) {
            val ackParams = AcknowledgePurchaseParams.newBuilder()
                .setPurchaseToken(purchase.purchaseToken)
                .build()
            val ackResult = billingClient.acknowledgePurchase(ackParams)
            if (ackResult.responseCode != BillingClient.BillingResponseCode.OK) {
                Log.e(TAG, "acknowledgePurchase failed: ${ackResult.debugMessage}")
                return
            }
        }

        purchase.products.forEach { productId ->
            activity.nativeOnPurchaseComplete(productId)
        }
    }
}
