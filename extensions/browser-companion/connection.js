"use strict";
// Connection page. It is the only place allowed to ask the worker to pair, and the
// worker verifies this exact page as the sender. Nothing here receives a credential:
// it reports a named state and a sanitized reason, nothing more.
const statusLabel = document.getElementById("status");
const pairButton = document.getElementById("pair");
const labels = {
  connected: "Connected over WebSocket",
  connecting: "Paired - connecting...",
  disconnected: "Paired - disconnected; retrying automatically",
  unpaired: "Not paired",
  error: "Connection needs attention",
};
const reasons = {
  desktop_unavailable: "Start ZenCrop, then pair again.",
  port_unavailable: "Another program is using ZenCrop's connection port.",
  pairing_timeout: "ZenCrop did not respond. Keep it running and try again.",
  pairing_disconnected: "ZenCrop closed the pairing request. Try again.",
  incompatible_desktop: "Update ZenCrop and this extension together.",
  authentication_failed: "Pair again to restore trusted access.",
  pair_again: "ZenCrop's pairing changed. Pair again.",
  pairing_store_invalid: "ZenCrop's stored pairing is damaged. Pair again to reset it.",
  credential_storage_unavailable: "Reload the extension and try again.",
  invalid_message: "The connection was rejected. Reload the extension and pair again.",
};
async function refresh() {
  try {
    const status = await chrome.runtime.sendMessage({type: "connection_status"});
    statusLabel.textContent = (labels[status?.state] || "Disconnected") +
      (status?.reason ? ". " + (reasons[status.reason] || "Check that ZenCrop is running and the bridge is registered in its settings.") : "");
    pairButton.textContent = status?.state === "connected" ? "Pair again" : "Pair with ZenCrop";
  } catch {
    statusLabel.textContent = "Extension unavailable. Reload it, then try again.";
  }
}
pairButton.addEventListener("click", async () => {
  pairButton.disabled = true;
  statusLabel.textContent = "Pairing...";
  try {
    await chrome.runtime.sendMessage({type: "pair"});
  } catch {
    statusLabel.textContent = "Pairing failed. Keep ZenCrop running and register the bridge in its settings.";
  } finally {
    pairButton.disabled = false;
    await refresh();
  }
});
void refresh();
setInterval(refresh, 1000);
