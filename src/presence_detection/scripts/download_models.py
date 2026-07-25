#!/usr/bin/env python3
import os
import sys
import json
import urllib.request
import logging

logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s', stream=sys.stdout)

def with_progress(func):
    """Decorator to inject a stateful progress hook into the download function."""
    def wrapper(url, output_path, **kwargs):
        last_percent = -1
        def progress_hook(count, block_size, total_size):
            nonlocal last_percent
            if total_size > 0:
                percent = int(count * block_size * 100 / total_size)
                percent = min(100, percent)
                if percent // 20 > last_percent // 20:
                    if 0 < percent < 100:
                        logging.info(f"Downloading... {percent}%")
                    last_percent = percent
        return func(url, output_path, reporthook=progress_hook, **kwargs)
    return wrapper

@with_progress
def download_file(url, output_path, reporthook=None):
    rel_path = os.path.relpath(output_path)
    if not os.path.exists(output_path):
        logging.info(f"Downloading to {rel_path}...")
        try:
            urllib.request.urlretrieve(url, output_path, reporthook=reporthook)
            logging.info("Download complete.")
        except Exception as e:
            logging.error(f"Failed to download {url}: {e}")
            raise
    else:
        logging.info(f"File already exists: {rel_path}. Skipping download.")

def create_symlink(target_rel_path, link_path):
    if os.path.exists(link_path) or os.path.islink(link_path):
        os.remove(link_path)
    os.symlink(target_rel_path, link_path)
    rel_link_path = os.path.relpath(link_path)
    logging.info(f"Created symlink: {rel_link_path} -> {target_rel_path}")

def get_github_latest_commit(repo, file_path):
    api_url = f"https://api.github.com/repos/{repo}/commits?path={file_path}&page=1&per_page=1"
    try:
        req = urllib.request.Request(api_url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read().decode())
            if data and len(data) > 0:
                return data[0].get("sha")
            return None
    except Exception as e:
        logging.error(f"Failed to fetch metadata from {api_url}: {e}")
        raise

def get_hf_latest_commit(repo_id, repo_type):
    api_url = f"https://huggingface.co/api/{repo_type}s/{repo_id}"
    try:
        with urllib.request.urlopen(api_url) as response:
            data = json.loads(response.read().decode())
            return data.get("sha")
    except Exception as e:
        logging.error(f"Failed to fetch metadata from {api_url}: {e}")
        raise

def process_model(model_cfg, models_dir):
    name = model_cfg["name"]
    source = model_cfg["source"]
    repo = model_cfg["repo"]
    file_path = model_cfg["file_path"]
    version_tag = model_cfg.get("version_tag", "")
    
    cloud_filename = os.path.basename(file_path)
    
    logging.info(f"Processing model: {name} from {source}...")
    
    latest_sha = None
    download_url = None
    
    if source == "github":
        latest_sha = model_cfg.get("locked_commit")
        if not latest_sha:
            latest_sha = get_github_latest_commit(repo, file_path)
            if not latest_sha:
                raise ValueError(f"Could not find commit for {file_path} in {repo}")
        download_url = f"https://github.com/{repo}/raw/{latest_sha}/{file_path}"
    
    elif source == "huggingface":
        repo_type = model_cfg.get("repo_type", "model")
        latest_sha = get_hf_latest_commit(repo, repo_type)
        if not latest_sha:
            raise ValueError(f"Could not find commit for {repo}")
            
        if repo_type == "dataset":
            download_url = f"https://huggingface.co/datasets/{repo}/resolve/{latest_sha}/{file_path}"
        else:
            download_url = f"https://huggingface.co/{repo}/resolve/{latest_sha}/{file_path}"
    else:
        raise ValueError(f"Unknown source: {source}")
        
    short_sha = latest_sha[:7]
    logging.info(f"Latest commit is: {short_sha}")
    
    folder_prefix = f"{version_tag}_{short_sha}" if version_tag else short_sha
    
    model_folder = os.path.join(models_dir, name)
    version_folder = os.path.join(model_folder, folder_prefix)
    os.makedirs(version_folder, exist_ok=True)
    
    target_file = os.path.join(version_folder, cloud_filename)
    download_file(download_url, target_file)
    
    symlink_path = os.path.join(models_dir, f"{name}.onnx")
    symlink_target = f"{name}/{folder_prefix}/{cloud_filename}"
    create_symlink(symlink_target, symlink_path)

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    models_dir = os.path.abspath(os.path.join(script_dir, "..", "models"))
    json_path = os.path.join(script_dir, "models.json")
    
    os.makedirs(models_dir, exist_ok=True)
    
    if not os.path.exists(json_path):
        logging.error(f"Schema file not found: {json_path}")
        sys.exit(1)
        
    with open(json_path, 'r') as f:
        models_cfg = json.load(f)
        
    for cfg in models_cfg:
        process_model(cfg, models_dir)
        
    logging.info("All models processed successfully!")

if __name__ == "__main__":
    main()
