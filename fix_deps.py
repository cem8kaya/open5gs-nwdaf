import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        if "github_token = getpass.getpass" in source:
            new_source = []
            for line in cell.get("source", []):
                if "echo '--> Compiling Release Build...'" in line:
                    new_source.append("    echo '--> Installing C++ Build Dependencies...'\n")
                    new_source.append("    sudo apt-get update >/dev/null\n")
                    new_source.append("    sudo apt-get install -y pkg-config build-essential cmake libyaml-cpp-dev libspdlog-dev libsystemd-dev >/dev/null\n\n")
                    new_source.append(line)
                else:
                    new_source.append(line)
            nb["cells"][i]["source"] = new_source

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
