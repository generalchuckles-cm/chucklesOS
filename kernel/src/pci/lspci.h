#ifndef LSPCI_H
#define LSPCI_H

// Runs the detailed, paged PCI list.
// Uses polling for pagination, so it blocks until finished.
// autoscroll: If true, waits 30s and automatically proceeds instead of waiting for keypress.
void lspci_run_detailed(bool autoscroll = false);

#endif