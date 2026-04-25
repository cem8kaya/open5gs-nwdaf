import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    # 1. Find and remove the deployment cells
    deploy_md_index = -1
    deploy_code_index = -1

    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        if "## 12.5 — Upgrade/Deploy Native C++ NWDAF" in source:
            deploy_md_index = i
        elif "=== Compiling and Deploying C++ NWDAF on VM ===" in source:
            deploy_code_index = i

    # Extract them
    if deploy_md_index != -1 and deploy_code_index != -1:
        # We delete from back to front to avoid shifting indices
        del nb["cells"][deploy_code_index]
        del nb["cells"][deploy_md_index]

    # 2. Create the new Python deployment cells
    new_deploy_md = {
        "cell_type": "markdown",
        "metadata": {},
        "source": [
            "## 8.0 — Upgrade/Deploy Native C++ NWDAF\n",
            "This cell replaces the older Python implementation with the high-performance C++ NWDAF.\n",
            "Because the GitHub repository is private, you will be prompted for your GitHub Personal Access Token (PAT) so the VM can clone it securely."
        ]
    }

    new_deploy_code = {
        "cell_type": "code",
        "execution_count": None,
        "metadata": {},
        "outputs": [],
        "source": [
            "import getpass\n",
            "import subprocess\n",
            "\n",
            "PROJECT_ID = \"g-ai-lab-491619\"\n",
            "ZONE = \"europe-west4-a\"\n",
            "VM_NAME = \"open5gs-ai-lab\"\n",
            "\n",
            "print(\"To deploy the C++ NWDAF, we need to clone the private repository on the VM.\")\n",
            "github_token = getpass.getpass(\"Enter your GitHub Personal Access Token (PAT): \")\n",
            "\n",
            "if not github_token:\n",
            "    print(\"Error: GitHub Token is required.\")\n",
            "else:\n",
            "    # Inject token into HTTPS URL securely\n",
            "    repo_url = f\"https://{github_token}@github.com/cem8kaya/open5gs-nwdaf.git\"\n",
            "    \n",
            "    bash_script = f\"\"\"\n",
            "    echo '--> Cloning latest source code from GitHub...'\n",
            "    rm -rf /tmp/open5gs-nwdaf\n",
            "    git clone {repo_url} /tmp/open5gs-nwdaf >/dev/null 2>&1\n",
            "    cd /tmp/open5gs-nwdaf || { echo 'Clone failed!'; exit 1; }\n",
            "\n",
            "    echo '--> Compiling Release Build...'\n",
            "    mkdir -p build-release && cd build-release\n",
            "    cmake .. -DCMAKE_BUILD_TYPE=Release -DNWDAF_USE_SD_JOURNAL=ON -DNWDAF_BUILD_TESTS=OFF >/dev/null\n",
            "    make -j\\$(nproc) >/dev/null\n",
            "\n",
            "    echo '--> Installing Binary and Service...'\n",
            "    sudo systemctl stop open5gs-nwdafd || true\n",
            "    sudo cp open5gs-nwdafd /usr/local/bin/\n",
            "    sudo chmod 755 /usr/local/bin/open5gs-nwdafd\n",
            "    cd ..\n",
            "\n",
            "    sudo rm -f /opt/nwdaf/nwdaf_server.py\n",
            "    sudo cp systemd/open5gs-nwdafd.service /etc/systemd/system/\n",
            "    \n",
            "    sudo mkdir -p /opt/nwdaf/models\n",
            "    sudo chown -R \\$(whoami):\\$(whoami) /opt/nwdaf\n",
            "    \n",
            "    sudo systemctl daemon-reload\n",
            "    sudo systemctl enable open5gs-nwdafd\n",
            "    sudo systemctl restart open5gs-nwdafd\n",
            "    \n",
            "    echo '✅ C++ NWDAF successfully deployed and running!'\n",
            "    \"\"\"\n",
            "\n",
            "    cmd = [\n",
            "        \"gcloud\", \"compute\", \"ssh\", VM_NAME,\n",
            "        f\"--project={PROJECT_ID}\",\n",
            "        f\"--zone={ZONE}\",\n",
            "        f\"--command={bash_script}\"\n",
            "    ]\n",
            "\n",
            "    print(\"Executing deployment on VM...\")\n",
            "    result = subprocess.run(cmd)\n",
            "    if result.returncode == 0:\n",
            "        print(\"\\nDeployment Complete.\")\n",
            "    else:\n",
            "        print(\"\\nDeployment Failed.\")\n"
        ]
    }

    # 3. Find where "8. NWDAF Operations" starts to insert it
    insert_index = -1
    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        if "# 8. NWDAF Operations" in source:
            insert_index = i + 1
            break

    if insert_index != -1:
        nb["cells"].insert(insert_index, new_deploy_code)
        nb["cells"].insert(insert_index, new_deploy_md)
    else:
        # Fallback to appending
        nb["cells"].extend([new_deploy_md, new_deploy_code])

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
