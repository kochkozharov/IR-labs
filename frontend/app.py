from flask import Flask, render_template, request, jsonify
import requests
import os

app = Flask(__name__)

ENGINE_URL = os.environ.get('ENGINE_URL', 'http://engine:9090')

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/search')
def search():
    query = request.args.get('q', '')
    page = request.args.get('page', '1')
    limit = request.args.get('limit', '50')
    
    try:
        resp = requests.get(f'{ENGINE_URL}/api/search', params={
            'q': query,
            'page': page,
            'limit': limit
        }, timeout=30)
        return jsonify(resp.json())
    except Exception as e:
        return jsonify({'error': str(e), 'results': [], 'total': 0}), 502

@app.route('/api/stats')
def stats():
    try:
        resp = requests.get(f'{ENGINE_URL}/api/stats', timeout=10)
        return jsonify(resp.json())
    except Exception as e:
        return jsonify({'error': str(e), 'status': 'unavailable'}), 502

@app.route('/api/zipf')
def zipf():
    limit = request.args.get('limit', '5000')
    try:
        resp = requests.get(f'{ENGINE_URL}/api/zipf', params={
            'limit': limit
        }, timeout=30)
        return jsonify(resp.json())
    except Exception as e:
        return jsonify({'error': str(e)}), 502

@app.route('/api/document')
def document():
    url = request.args.get('url', '')
    try:
        resp = requests.get(f'{ENGINE_URL}/api/document', params={
            'url': url
        }, timeout=10)
        return (jsonify(resp.json()), resp.status_code)
    except Exception as e:
        return jsonify({'error': str(e)}), 502

if __name__ == '__main__':
    print(f"Frontend starting, engine URL: {ENGINE_URL}")
    app.run(host='0.0.0.0', port=8080, debug=False)
