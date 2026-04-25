import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    ui_md = {
        "cell_type": "markdown",
        "metadata": {},
        "source": [
            "## 14. Web UI Integration & Comparison\n",
            "The NWDAF Operations Dashboard provides a visual representation of the analytics data.\n",
            "Run the cell below to retrieve the public IP address of the dashboard, check the NWDAF status, and fetch the latest analytics results. You can use these terminal outputs to cross-reference and verify the data displayed in the React web UI."
        ]
    }
    
    ui_code = {
        "cell_type": "code",
        "execution_count": None,
        "metadata": {},
        "outputs": [],
        "source": [
            "%%bash\n",
            "PROJECT_ID=\"g-ai-lab-491619\"; ZONE=\"europe-west4-a\"; VM_NAME=\"open5gs-ai-lab\"\n",
            "\n",
            "echo '=== Fetching NWDAF Web UI IP Address ==='\n",
            "EXTERNAL_IP=$(gcloud compute instances describe $VM_NAME \\\n",
            "    --project=$PROJECT_ID \\\n",
            "    --zone=$ZONE \\\n",
            "    --format='get(networkInterfaces[0].accessConfigs[0].natIP)')\n",
            "\n",
            "echo \"🌐 Access the NWDAF Dashboard at: http://$EXTERNAL_IP\"\n",
            "echo \"(Note: Ensure the correct port is appended if not running on 80, e.g., http://$EXTERNAL_IP:3000)\"\n",
            "echo \"\"\n",
            "\n",
            "echo '=== Current NWDAF Service Status ==='\n",
            "gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command=\"\n",
            "    curl -s http://localhost:7779/nwdaf-analytics/v1/health | python3 -m json.tool\n",
            "\"\n",
            "\n",
            "echo ''\n",
            "echo '=== Latest Analytics Results for UI Comparison ==='\n",
            "gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command=\"\n",
            "    for ID in NF_LOAD UE_MOBILITY UE_COMMUNICATION ABNORMAL_BEHAVIOUR QoS_SUSTAINABILITY SERVICE_EXPERIENCE NETWORK_PERFORMANCE; do\n",
            "        echo \\\"--- \\$ID ---\\\"\n",
            "        # Print just the analData object to keep the output concise for comparison\n",
            "        curl -s \\\"http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=\\$ID\\\" | python3 -c '\n",
            "import sys, json\n",
            "try:\n",
            "    d = json.load(sys.stdin)\n",
            "    print(json.dumps(d.get(\\\"analData\\\", d), indent=2))\n",
            "except:\n",
            "    pass' || echo 'Failed to fetch'\n",
            "    done\n",
            "\"\n"
        ]
    }
    
    nb["cells"].extend([ui_md, ui_code])

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
