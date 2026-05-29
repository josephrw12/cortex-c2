from flask import Flask, send_file, abort
import os

app = Flask(__name__)

# Define the absolute path to the plugins folder
PLUGINS_DIR = os.path.join(app.root_path, 'plugins')

@app.route('/plugins/<filename>')
def download_plugin(filename):
    """Endpoint to download a specific plugin from the plugins folder."""
    # Construct the full path to the file securely
    file_path = os.path.join(PLUGINS_DIR, filename)
    
    # Check if the file exists inside the plugins folder
    if not os.path.isfile(file_path):
        abort(404, description="Plugin not found")
        
    return send_file(file_path, as_attachment=True)

if __name__ == '__main__':
    # Create the directory automatically for testing if it doesn't exist
    if not os.path.exists(PLUGINS_DIR):
        os.makedirs(PLUGINS_DIR)
        # Create a dummy file for verification
        with open(os.path.join(PLUGINS_DIR, 'plugin-1'), 'w') as f:
            f.write('Sample data for plugin-1')

    # Running on port 80 to match your URL http://127.0.0.1/plugins/plugin-1
    # Note: Running on port 80 may require administrator/sudo privileges
    app.run(debug=True, host='127.0.0.1', port=5000)
