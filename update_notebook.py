import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        
        # 1. Replace the old Python ML Training script
        if "anomaly_model.pkl" in source and "IsolationForest" in source:
            new_source = [
                "%%bash\n",
                "PROJECT_ID=\"g-ai-lab-491619\"; ZONE=\"europe-west4-a\"; VM_NAME=\"open5gs-ai-lab\"\n",
                "\n",
                "gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command=\"\n",
                "echo '=== Triggering Native C++ ML Model Training ==='\n",
                "echo 'Training Isolation Forest...'\n",
                "curl -s -X POST http://localhost:7779/nwdaf-analytics/v1/train | python3 -m json.tool\n",
                "\"\n"
            ]
            nb["cells"][i]["source"] = new_source
            nb["cells"][i]["outputs"] = []
            nb["cells"][i]["execution_count"] = None
        
        else:
            # 2 & 3. Update paths in other cells
            if "/opt/nwdaf/nwdaf_models/" in source:
                new_source = source.replace("/opt/nwdaf/nwdaf_models/", "/opt/nwdaf/models/")
                nb["cells"][i]["source"] = [line + "\n" if j < len(new_source.split("\n")) - 1 else line for j, line in enumerate(new_source.split("\n"))]
            
            # Special case for Cell 86 where it mentions the config path in a markdown table
            if "| NWDAF | `open5gs-nwdafd` |" in source and "/opt/nwdaf/" in source:
                new_source = source.replace("| `/opt/nwdaf/` |", "| `/etc/open5gs/nwdaf.yaml` |")
                nb["cells"][i]["source"] = [line + "\n" if j < len(new_source.split("\n")) - 1 else line for j, line in enumerate(new_source.split("\n"))]

    # 4. Append new verification cells
    verification_md = {
        "cell_type": "markdown",
        "metadata": {},
        "source": [
            "## 13. NWDAF C++ Verification\n",
            "These cells interact with the C++ NWDAF REST API directly to verify full integration."
        ]
    }
    
    verification_health = {
        "cell_type": "code",
        "execution_count": None,
        "metadata": {},
        "outputs": [],
        "source": [
            "%%bash\n",
            "PROJECT_ID=\"g-ai-lab-491619\"; ZONE=\"europe-west4-a\"; VM_NAME=\"open5gs-ai-lab\"\n",
            "\n",
            "gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command=\"\n",
            "echo '=== NWDAF Health Check ==='\n",
            "curl -s http://localhost:7779/nwdaf-analytics/v1/health | python3 -m json.tool\n",
            "\"\n"
        ]
    }
    
    verification_analytics = {
        "cell_type": "code",
        "execution_count": None,
        "metadata": {},
        "outputs": [],
        "source": [
            "%%bash\n",
            "PROJECT_ID=\"g-ai-lab-491619\"; ZONE=\"europe-west4-a\"; VM_NAME=\"open5gs-ai-lab\"\n",
            "\n",
            "gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command=\"\n",
            "for ID in NF_LOAD UE_MOBILITY UE_COMMUNICATION ABNORMAL_BEHAVIOUR QoS_SUSTAINABILITY SERVICE_EXPERIENCE NETWORK_PERFORMANCE; do\n",
            "    echo \\\"=== \\$ID ===\\\"\n",
            "    curl -s \\\"http://localhost:7779/nwdaf-analytics/v1/analytics?analyticsId=\\$ID\\\" | python3 -m json.tool\n",
            "    echo \\\"\\\"\n",
            "done\n",
            "\"\n"
        ]
    }
    
    nb["cells"].extend([verification_md, verification_health, verification_analytics])

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
