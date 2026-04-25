import json

def process_notebook(filepath):
    with open(filepath, "r") as f:
        nb = json.load(f)

    for i, cell in enumerate(nb["cells"]):
        source = "".join(cell.get("source", []))
        if "sudo cp systemd/open5gs-nwdafd.service /etc/systemd/system/" in source:
            new_source = []
            for line in cell.get("source", []):
                new_source.append(line)
                if "sudo cp systemd/open5gs-nwdafd.service /etc/systemd/system/" in line:
                    new_source.append("    # Patch the service file to remove the broken watchdog timeout\n")
                    new_source.append("    sudo sed -i '/WatchdogSec=30/d' /etc/systemd/system/open5gs-nwdafd.service\n")
            nb["cells"][i]["source"] = new_source

    with open(filepath, "w") as f:
        json.dump(nb, f, indent=1)
        f.write("\n")

process_notebook("5g_ai_lab_daily_ops_v1.ipynb")
