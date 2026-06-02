# NWDAF Operations Dashboard - Quick Start

This guide explains how to run and connect the NWDAF Operations Dashboard to your remote GCP `open5gs-nwdafd` instance.

## 1. Opening in Claude Artifacts (or any React runner)

The `nwdaf_dashboard.jsx` file is a standalone React application designed to run in environments like Claude Artifacts, CodeSandbox, or a local Vite setup.
- If using Claude, simply paste the contents of `nwdaf_dashboard.jsx` into an artifact window.
- It requires `react`, `lucide-react`, and `recharts`. Tailwind CSS utility classes are used for styling.

## 2. Setting up the Connection (gcloud port-forward)

To allow your local browser to communicate with the remote NWDAF REST API, you need to set up a port forward. 

Run the following command in your local terminal:

```bash
gcloud compute ssh open5gs-ai-lab \
  --zone=europe-west4-a \
  --project=g-ai-lab-491619 \
  -- -L 7779:localhost:7779 -N
```

This will forward your local port `7779` to the remote VM's port `7779` securely over SSH.

## 3. Configuring the Base URL in Settings

1. Open the Dashboard.
2. Navigate to the **Settings** page (using the sidebar or by pressing the `?` key to see shortcuts).
3. Under **Connection Configuration**, ensure the Base URL is set to:
   `http://localhost:7779/nwdaf-analytics/v1`
4. Select the **gcloud port-forward** mode to keep the command handy.
5. Click **Test Connection** to verify that the dashboard can reach the API. The top navigation bar should show a green `🟢 Connected` badge.

## 4. Overview Page Layout

The Overview page serves as your command center:

- **Top Row (Status Cards)**: 
  - **NWDAF STATUS**: Overall service health, port, and UUID.
  - **NF HEALTH**: Proportion of stable Network Functions.
  - **THROUGHPUT**: Current Downlink (DL) and Uplink (UL) bandwidth in kbps.
- **Bottom Row (Analytics Cards)**:
  - **ANOMALY STATUS**: Isolation Forest model status, anomaly percentage.
  - **MOS SCORE**: Mean Opinion Score (1.0 - 5.0) for user experience.
  - **NET PERFORMANCE**: Aggregate network score out of 100.
- **Live Throughput Chart**: A scrolling area chart showing the last 30 seconds of DL/UL throughput.
- **Recent Activity Feed**: A live log of the last 10 API requests, showing latency and status.

Enjoy monitoring your 5G Core!
