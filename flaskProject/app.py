from flask import Flask, render_template, request, jsonify
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy import asc
from flask_cors import cross_origin
from datetime import datetime
import json
import requests
import pytz

app = Flask(__name__)
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///site.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
db = SQLAlchemy(app)

esp_ip = 'http://192.168.1.2'


class Measurements(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    timestamp = db.Column(db.Integer, default=int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3)
    PhaseVA = db.Column(db.Float, nullable=False)
    PhaseVB = db.Column(db.Float, nullable=False)
    PhaseVC = db.Column(db.Float, nullable=False)
    PhaseIA = db.Column(db.Float, nullable=False)
    PhaseIB = db.Column(db.Float, nullable=False)
    PhaseIC = db.Column(db.Float, nullable=False)

    def __repr__(self):
        return '<Measurements %r>' % self.id


@app.route('/')
@cross_origin()
def home():
    return render_template('index.html')


@app.route('/measurements', methods=['POST'])
def receive_data():
    if request.method == 'POST':
        data = request.get_data()
        try:
            json_data = json.loads(data)
            data_array = json_data.get('data')

            current_time = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60 * 60 * 3
            time_needs_sync = False

            values_phase_va = []
            values_phase_vb = []
            values_phase_vc = []
            values_phase_ia = []
            values_phase_ib = []
            values_phase_ic = []
            time_values = []
            k = 0
            for item in data_array:
                phase_va = item.get('PhaseVA')/4672
                phase_va = round(phase_va, 2)
                phase_vb = item.get('PhaseVB')/4672
                phase_vb = round(phase_vb, 2)
                phase_vc = item.get('PhaseVC')/4672
                phase_vc = round(phase_vc, 2)
                phase_ia = (item.get('PhaseIA')/1914753)*100
                phase_ia = round(phase_ia, 3)
                phase_ib = (item.get('PhaseIB')/1914753)*100
                phase_ib = round(phase_ib, 3)
                phase_ic = (item.get('PhaseIC')/1914753)*100
                phase_ic = round(phase_ic, 3)
                if phase_va > 500:
                    phase_va = 500
                if phase_vb > 500:
                    phase_vb = 500
                if phase_vc > 500:
                    phase_vc = 500

                time = item.get('Time')

                if time < 1000000000:
                    time_needs_sync = True
                    time = current_time + k

                values_phase_va.append(phase_va)
                values_phase_vb.append(phase_vb)
                values_phase_vc.append(phase_vc)
                values_phase_ia.append(phase_ia)
                values_phase_ib.append(phase_ib)
                values_phase_ic.append(phase_ic)
                time_values.append(time)
                k = k + 1

            if time_needs_sync:
                response = requests.get(esp_ip + "/updateTime")
                print(response.text)

            for i in range(0, 50, 5):
                subset_va = values_phase_va[i:i + 5]
                subset_vb = values_phase_vb[i:i + 5]
                subset_vc = values_phase_vc[i:i + 5]
                subset_ia = values_phase_ia[i:i + 5]
                subset_ib = values_phase_ib[i:i + 5]
                subset_ic = values_phase_ic[i:i + 5]
                subset_time = time_values[i:i + 5]

                average_value_va = sum(subset_va) / len(subset_va)
                average_value_vb = sum(subset_vb) / len(subset_vb)
                average_value_vc = sum(subset_vc) / len(subset_vc)
                average_value_ia = sum(subset_ia) / len(subset_ia)
                average_value_ib = sum(subset_ib) / len(subset_ib)
                average_value_ic = sum(subset_ic) / len(subset_ic)
                average_value_time = int(sum(subset_time) / len(subset_time))

                measurement = Measurements(PhaseVA=average_value_va, PhaseVB=average_value_vb, PhaseVC=average_value_vc,
                                           PhaseIA=average_value_ia, PhaseIB=average_value_ib, PhaseIC=average_value_ic,
                                           timestamp=average_value_time)
                db.session.add(measurement)
            db.session.commit()
            return "Data received and processed successfully", 200
        except ValueError as e:
            print("Received data is not a valid JSON:", e)
            return "Invalid JSON data", 400


@app.route('/espSettings', methods=['GET'])
@cross_origin()
def get_settings():
    esp8266_url = esp_ip + '/config.json'

    try:
        response = requests.get(esp8266_url)

        data_dict = json.loads(response.text)
        print(data_dict)
        if response.status_code == 200:
            return data_dict
        else:
            return "Failed to retrieve config from ESP8266", 500
    except requests.exceptions.RequestException as e:
        return "Error accessing ESP8266: {}".format(str(e)), 500


@app.route('/espState', methods=['GET'])
@cross_origin()
def get_state():
    esp8266_url = esp_ip + '/state.json'

    try:
        response = requests.get(esp8266_url)

        data_dict = json.loads(response.text)

        if response.status_code == 200:
            print(data_dict)
            return data_dict
        else:
            return "Failed to retrieve state from ESP8266", 500
    except requests.exceptions.RequestException as e:
        return "Error accessing ESP8266: {}".format(str(e)), 500


@app.route('/espChangeSettings', methods=['POST'])
@cross_origin()
def change_settings():
    esp8266_url = esp_ip + '/settings'
    if request.method == 'POST':
        data = request.get_data()
        try:
            json_data = json.loads(data)
        except ValueError as e:
            print("Received data is not a valid JSON:", e)
            return "Invalid JSON data", 400

        try:
            response = requests.post(esp8266_url, json=json_data)
            response.raise_for_status()
        except requests.exceptions.RequestException as e:
            print("Error sending data to ESP8266:", e)
            return f"Failed to send data to ESP8266: {e}", 500

        return jsonify({"message": "Data successfully sent to ESP8266", "status": response.status_code}), 200


@app.route('/light', methods=['GET'])
@cross_origin()
def light_control():
    state = request.args.get('state')
    if state is None:
        return jsonify({"error": "Missing 'state' parameter"}), 400

    try:
        state = int(state)
    except ValueError:
        return jsonify({"error": "Invalid 'state' parameter"}), 400

    if state == 1:
        esp8266_url = esp_ip + '/light?state=1'
        response = requests.get(esp8266_url)
        if response.status_code == 200:
            text = response.text
        else:
            text = "Error sending data to ESP8266"
    elif state == 0:
        esp8266_url = esp_ip + '/light?state=0'
        response = requests.get(esp8266_url)
        if response.status_code == 200:
            text = response.text
        else:
            text = "Error sending data to ESP8266"
    else:
        text = "Invalid state value"

    return text


@app.route('/database', methods=['GET'])
@cross_origin()
def get_database():
    from_value = request.args.get('from')
    to_value = request.args.get('to')
    if from_value is None:
        from_value = 0
    else:
        from_value = int(from_value)

    if to_value is None:
        to_value = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3
    else:
        to_value = int(to_value)

    measurements = (Measurements.query.filter(Measurements.timestamp >= from_value, Measurements.timestamp <= to_value)
                    .all())
    data = [{'PhaseVA': m.PhaseVA, 'PhaseVB': m.PhaseVB, 'PhaseVC': m.PhaseVC, 'PhaseIA': m.PhaseIA,
             'PhaseIB': m.PhaseIB, 'PhaseIC': m.PhaseIC, 'timestamp': m.timestamp} for m in measurements]
    return jsonify({'data': data})


@app.route('/database/phaseVA', methods=['GET'])
@cross_origin()
def get_database_va():
    from_value = request.args.get('from')
    to_value = request.args.get('to')
    if from_value is None:
        from_value = 0
    else:
        from_value = int(from_value)

    if to_value is None:
        to_value = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3
    else:
        to_value = int(to_value)

    measurements = Measurements.query.filter(Measurements.timestamp >= from_value, Measurements.timestamp <= to_value) \
        .order_by(asc(Measurements.timestamp)).all()
    data = [{'PhaseVA': m.PhaseVA, 'timestamp': m.timestamp} for m in measurements]
    return jsonify({'data': data})


@app.route('/database/phaseVB', methods=['GET'])
@cross_origin()
def get_database_vb():
    from_value = request.args.get('from')
    to_value = request.args.get('to')
    if from_value is None:
        from_value = 0
    else:
        from_value = int(from_value)

    if to_value is None:
        to_value = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3
    else:
        to_value = int(to_value)

    measurements = Measurements.query.filter(Measurements.timestamp >= from_value, Measurements.timestamp <= to_value) \
        .order_by(asc(Measurements.timestamp)).all()
    data = [{'PhaseVB': m.PhaseVB, 'timestamp': m.timestamp} for m in measurements]
    return jsonify({'data': data})


@app.route('/database/phaseVC', methods=['GET'])
@cross_origin()
def get_database_vc():
    from_value = request.args.get('from')
    to_value = request.args.get('to')
    if from_value is None:
        from_value = 0
    else:
        from_value = int(from_value)

    if to_value is None:
        to_value = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3
    else:
        to_value = int(to_value)

    measurements = Measurements.query.filter(Measurements.timestamp >= from_value, Measurements.timestamp <= to_value) \
        .order_by(asc(Measurements.timestamp)).all()
    data = [{'PhaseVC': m.PhaseVC, 'timestamp': m.timestamp} for m in measurements]
    return jsonify({'data': data})


@app.route('/database/phaseIA', methods=['GET'])
@cross_origin()
def get_database_ia():
    from_value = request.args.get('from')
    to_value = request.args.get('to')
    if from_value is None:
        from_value = 0
    else:
        from_value = int(from_value)

    if to_value is None:
        to_value = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3
    else:
        to_value = int(to_value)

    measurements = Measurements.query.filter(Measurements.timestamp >= from_value, Measurements.timestamp <= to_value) \
        .order_by(asc(Measurements.timestamp)).all()
    data = [{'PhaseIA': m.PhaseIA, 'timestamp': m.timestamp} for m in measurements]
    return jsonify({'data': data})


@app.route('/database/phaseIB', methods=['GET'])
@cross_origin()
def get_database_ib():
    from_value = request.args.get('from')
    to_value = request.args.get('to')
    if from_value is None:
        from_value = 0
    else:
        from_value = int(from_value)

    if to_value is None:
        to_value = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3
    else:
        to_value = int(to_value)

    measurements = Measurements.query.filter(Measurements.timestamp >= from_value, Measurements.timestamp <= to_value) \
        .order_by(asc(Measurements.timestamp)).all()
    data = [{'PhaseIB': m.PhaseIB, 'timestamp': m.timestamp} for m in measurements]
    return jsonify({'data': data})


@app.route('/database/phaseIC', methods=['GET'])
@cross_origin()
def get_database_ic():
    from_value = request.args.get('from')
    to_value = request.args.get('to')
    if from_value is None:
        from_value = 0
    else:
        from_value = int(from_value)

    if to_value is None:
        to_value = int(datetime.now(pytz.timezone('Europe/Kiev')).timestamp()) + 60*60*3
    else:
        to_value = int(to_value)

    measurements = Measurements.query.filter(Measurements.timestamp >= from_value, Measurements.timestamp <= to_value) \
        .order_by(asc(Measurements.timestamp)).all()
    data = [{'PhaseIC': m.PhaseIC, 'timestamp': m.timestamp} for m in measurements]
    return jsonify({'data': data})


@app.route('/time', methods=['GET'])
def get_time():
    current_time = datetime.now(pytz.timezone('Europe/Kiev'))
    unix_time = int(current_time.timestamp()) + 60 * 60 * 3
    time_data = {
        'current_time': unix_time
    }
    return jsonify(time_data)


if __name__ == '__main__':
    app.run()

