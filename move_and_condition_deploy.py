import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    # 1. Find and remove the deploy cell and markdown
    deploy_md_index = -1
    deploy_code_index = -1

    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        if "## 8.0 — Upgrade/Deploy Native C++ NWDAF" in source:
            deploy_md_index = i
        elif "github_token = getpass.getpass" in source:
            deploy_code_index = i

    if deploy_md_index != -1 and deploy_code_index != -1:
        del nb["cells"][deploy_code_index]
        del nb["cells"][deploy_md_index]

    # 2. Create the new conditional Python cell
    new_deploy_md = {
        "cell_type": "markdown",
        "metadata": {},
        "source": [
            "## 1.4 — Upgrade/Deploy Native C++ NWDAF\n",
            "This cell checks if the native C++ NWDAF is already deployed on the VM. If not, it clones the private repository and builds it. It will prompt for your GitHub PAT if a deployment is required."
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
            "# Check if already deployed\n",
            "check_cmd = [\n",
            "    \"gcloud\", \"compute\", \"ssh\", VM_NAME, \n",
            "    f\"--project={PROJECT_ID}\", f\"--zone={ZONE}\", \n",
            "    \"--command=test -f /usr/local/bin/open5gs-nwdafd && echo 'EXISTS' || echo 'MISSING'\"\n",
            "]\n",
            "print(\"Checking VM for existing C++ NWDAF installation...\")\n",
            "check_res = subprocess.run(check_cmd, capture_output=True, text=True)\n",
            "\n",
            "if \"EXISTS\" in check_res.stdout:\n",
            "    print(\"✅ C++ NWDAF is already deployed and installed on the VM.\")\n",
            "    force = input(\"Do you want to force re-deploy and rebuild from the latest GitHub code? (y/N): \")\n",
            "    if force.lower() != 'y':\n",
            "        print(\"Skipping deployment.\")\n",
            "        github_token = None\n",
            "    else:\n",
            "        github_token = getpass.getpass(\"Enter your GitHub Personal Access Token (PAT): \")\n",
            "else:\n",
            "    print(\"C++ NWDAF not found. A deployment is required.\")\n",
            "    github_token = getpass.getpass(\"Enter your GitHub Personal Access Token (PAT): \")\n",
            "\n",
            "if github_token:\n",
            "    repo_url = f\"https://{github_token}@github.com/cem8kaya/open5gs-nwdaf.git\"\n",
            "    \n",
            "    bash_script = f\"\"\"\n",
            "    set -e\n",
            "    echo '--> Cloning latest source code from GitHub...'\n",
            "    rm -rf /tmp/open5gs-nwdaf\n",
            "    git clone {repo_url} /tmp/open5gs-nwdaf >/dev/null 2>&1\n",
            "    cd /tmp/open5gs-nwdaf || {{ echo 'Clone failed!'; exit 1; }}\n",
            "\n",
            "    echo '--> Installing C++ Build Dependencies...'\n",
            "    sudo DEBIAN_FRONTEND=noninteractive apt-get update >/dev/null\n",
            "    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y pkg-config build-essential cmake libyaml-cpp-dev libspdlog-dev libsystemd-dev >/dev/null\n",
            "\n",
            "    echo '--> Compiling Release Build...'\n",
            "    mkdir -p build-release && cd build-release\n",
            "    cmake .. -DCMAKE_BUILD_TYPE=Release -DNWDAF_USE_SD_JOURNAL=ON -DNWDAF_BUILD_TESTS=OFF >/dev/null\n",
            "    make -j$(nproc) >/dev/null\n",
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
            "    sudo chown -R $(whoami):$(whoami) /opt/nwdaf\n",
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
            "    result = subprocess.run(cmd, capture_output=True, text=True)\n",
            "    \n",
            "    print(result.stdout)\n",
            "    if result.returncode != 0:\n",
            "        print(\"\\n--- Errors / Warnings ---\")\n",
            "        print(result.stderr)\n",
            "        print(\"\\nDeployment Failed.\")\n",
            "    else:\n",
            "        print(\"Deployment Complete.\")\n"
        ]
    }

    # 3. Find where to insert it (beginning of notebook, perhaps after 1.3)
    insert_index = -1
    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        if "## 1.3 — Colab-side Python Dependencies" in source:
            # We want to insert after the code cell following this markdown
            insert_index = i + 2
            break

    if insert_index != -1:
        nb["cells"].insert(insert_index, new_deploy_code)
        nb["cells"].insert(insert_index, new_deploy_md)
    else:
        nb["cells"].insert(3, new_deploy_code)
        nb["cells"].insert(3, new_deploy_md)

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
