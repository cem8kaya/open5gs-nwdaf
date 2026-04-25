import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        
        if "12.5 — NWDAF Reinstall" in source:
            nb["cells"][i]["source"] = [
                "## 12.5 — Upgrade/Deploy Native C++ NWDAF\n",
                "Use this cell to remove the older Python implementation of NWDAF and deploy the highly-performant, native C++ version from your local workspace."
            ]
            
        elif "NWDAF uninstalled — re-run Cells" in source:
            new_source = [
                "%%bash\n",
                "PROJECT_ID=\"g-ai-lab-491619\"; ZONE=\"europe-west4-a\"; VM_NAME=\"open5gs-ai-lab\"\n",
                "\n",
                "echo '=== 1. Archiving Local C++ Source ==='\n",
                "# Compress the local C++ workspace (excluding build directories and git history)\n",
                "tar -czf /tmp/nwdaf-cpp-src.tar.gz --exclude='build*' --exclude='.git' .\n",
                "\n",
                "echo '=== 2. Uploading to VM ==='\n",
                "gcloud compute scp /tmp/nwdaf-cpp-src.tar.gz $VM_NAME:/tmp/ \\\n",
                "    --project=$PROJECT_ID --zone=$ZONE\n",
                "\n",
                "echo '=== 3. Compiling and Deploying on VM ==='\n",
                "gcloud compute ssh $VM_NAME --project=$PROJECT_ID --zone=$ZONE --command=\"\n",
                "    # Extract source\n",
                "    rm -rf /tmp/nwdaf-cpp && mkdir -p /tmp/nwdaf-cpp\n",
                "    tar -xzf /tmp/nwdaf-cpp-src.tar.gz -C /tmp/nwdaf-cpp\n",
                "    cd /tmp/nwdaf-cpp\n",
                "\n",
                "    echo '--> Compiling Release Build...'\n",
                "    mkdir -p build-release && cd build-release\n",
                "    cmake .. -DCMAKE_BUILD_TYPE=Release -DNWDAF_USE_SD_JOURNAL=ON -DNWDAF_BUILD_TESTS=OFF\n",
                "    make -j\\$(nproc)\n",
                "\n",
                "    echo '--> Installing Binary and Service...'\n",
                "    sudo systemctl stop open5gs-nwdafd || true\n",
                "    sudo cp open5gs-nwdafd /usr/local/bin/\n",
                "    sudo chmod 755 /usr/local/bin/open5gs-nwdafd\n",
                "    cd ..\n",
                "    \n",
                "    # Clean up old Python implementation files if they exist\n",
                "    sudo rm -f /opt/nwdaf/nwdaf_server.py\n",
                "    \n",
                "    # Overwrite the old Python systemd unit with the C++ one\n",
                "    sudo cp systemd/open5gs-nwdafd.service /etc/systemd/system/\n",
                "    \n",
                "    # Setup directories\n",
                "    sudo mkdir -p /opt/nwdaf/models\n",
                "    sudo chown -R \\$(whoami):\\$(whoami) /opt/nwdaf\n",
                "    \n",
                "    # Restart the service to boot the C++ daemon\n",
                "    sudo systemctl daemon-reload\n",
                "    sudo systemctl enable open5gs-nwdafd\n",
                "    sudo systemctl restart open5gs-nwdafd\n",
                "    \n",
                "    echo '✅ C++ NWDAF successfully deployed and running!'\n",
                "\"\n"
            ]
            nb["cells"][i]["source"] = new_source
            nb["cells"][i]["outputs"] = []
            nb["cells"][i]["execution_count"] = None

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
