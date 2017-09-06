/**
 * @file   ArdusatSDK.cpp
 * @author Ben Peters (ben@ardusat.com)
 * @author Sam Olds (sam@ardusat.com)
 * @date   December 3, 2014
 * @brief  Implements ArdusatSDK generic sensor reading and configuration for Space Kit Sensors.
 *
 * Provides a unifying wrapper of sensor specific functionality to provide a
 * consistent interface to interact with each type of sensor.
 */

#include <stdio.h>
#include <string.h>
#include "ArdusatSDK.h"

boolean MANUAL_CONFIG = false;
boolean ARDUSAT_SPACEBOARD = false;
int OUTPUT_BUF_SIZE = 256;
char * _output_buffer;
static int _output_buf_len = 0;

const char concat_string[] PROGMEM = "%s%s";
// TODO: Change these error messages to be JSON that can easily be caught by the Experiment Platform
const char begin_error_msg[] PROGMEM = "begin %s failed. Check wiring!";
const char unavailable_on_hardware_error_msg[] PROGMEM = "%s is not available with %s";

const char spacekit_hardware_name[] PROGMEM = "Space Kit";
const char spaceboard_hardware_name[] PROGMEM = "SpaceBoard";

const char acceleration_sensor_name[] PROGMEM = "accelerometer";
const char gyro_sensor_name[] PROGMEM = "gyro";
const char luminosity_sensor_name[] PROGMEM = "luminosity";
const char magnetic_sensor_name[] PROGMEM = "magnetic";
const char orientation_sensor_name[] PROGMEM = "orientation";
const char orientation_sensor_roll[] PROGMEM = "Roll";
const char orientation_sensor_pitch[] PROGMEM = "Pitch";
const char orientation_sensor_heading[] PROGMEM = "Heading";
const char pressure_sensor_name[] PROGMEM = "pressure";
const char temperature_sensor_name[] PROGMEM = "ambientTemp";
const char irtemperature_sensor_name[] PROGMEM = "infraredTemp";
const char rgblight_sensor_name[] PROGMEM = "rgb";
const char rgblight_sensor_red[] PROGMEM = "Red";
const char rgblight_sensor_green[] PROGMEM = "Green";
const char rgblight_sensor_blue[] PROGMEM = "Blue";
const char uvlight_sensor_name[] PROGMEM = "uv";

const char CSV_TIMESTAMP[] PROGMEM = "timestamp(milliseconds)";
const char CSV_CHECKSUM[] PROGMEM = "checksum";

static char CSV_SEPARATOR = ',';
static char JSON_PREFIX = '~';
static char JSON_SUFFIX = '|';
const char json_format[] PROGMEM = "%c{\"sensorName\":\"%s\",\"unit\":\"%s\",\"value\":%s,\"cs\":%d}%c\n";

/*
 * Gets the output buffer used for storing sensor data, or initializes
 * it if it doesn't yet exist
 *
 * @return the current output buffer
 */
char * _getOutBuf() {
  if (_output_buffer == NULL) {
    _output_buffer = new char[OUTPUT_BUF_SIZE];
  }
  return _output_buffer;
}

/*
 * Resets the output buffer to be blank
 */
void _resetOutBuf() {
  memset(_getOutBuf(), 0, OUTPUT_BUF_SIZE);
  _output_buf_len = 0;
}

/**
 * Convert an enumerated unit code to a string representation.
 *
 * @param unit code
 *
 * @return string representation of unit
 */
const char * unit_to_str(unsigned char unit) {
  switch (unit) {
    case (DATA_UNIT_NONE):
      return "";
    case (DATA_UNIT_METER_PER_SECONDSQUARED):
      return "m/s^2";
    case (DATA_UNIT_RADIAN_PER_SECOND):
      return "rad/s";
    case (DATA_UNIT_MICROTESLA):
      return "uT";
    case (DATA_UNIT_DEGREES_CELSIUS):
      return "C";
    case (DATA_UNIT_METER_PER_SECOND):
      return "m/s";
    case (DATA_UNIT_LUX):
      return "lux";
    case (DATA_UNIT_MILLIWATT_PER_CMSQUARED):
      return "mW/cm^2";
    case (DATA_UNIT_DEGREES):
      return "deg";
    case (DATA_UNIT_HECTOPASCAL):
      return "hPa";
    case (DATA_UNIT_FEET):
      return "ft";
    case (DATA_UNIT_METERS):
      return "m";
    case (DATA_UNIT_MILLISECONDS):
      return "ms";
    default:
      return "";
  };
}

/*
 * Prints an error message that has exactly two "%s" format specifiers in error_msg
 * This relies on a 256 character output buffer. Make sure that sensorName isn't too long!
 *
 * @param error_msg the base error message
 * @param sensorName name of sensor that failed.
 * @param hardwareBuild empty string, space kit, or spaceboard
 */
void _writeErrorMessage(const char error_msg[] PROGMEM, const char sensorName[] PROGMEM, const char hardwareBuild[] PROGMEM) {
  char err_msg[30];
  char sensor[50];
  char hardware[15];

  strcpy_P(err_msg, error_msg);
  strcpy_P(sensor, sensorName);
  strcpy_P(hardware, hardwareBuild);

  // Make SURE sensorName isn't too long for the output buffer!!!
  _resetOutBuf();
  sprintf(_getOutBuf(), err_msg, sensor, hardware);
  Serial.println(_getOutBuf());
}

/*
 * Prints an error message that has exactly one "%s" format specifiers in error_msg
 * This relies on a 256 character output buffer. Make sure that sensorName isn't too long!
 *
 * @param error_msg the base error message
 * @param sensorName name of sensor that failed.
 */
void _writeErrorMessage(const char error_msg[] PROGMEM, const char sensorName[] PROGMEM) {
  char err_msg[30];
  char sensor[50];

  strcpy_P(err_msg, error_msg);
  strcpy_P(sensor, sensorName);

  // Make SURE sensorName isn't too long for the output buffer!!!
  _resetOutBuf();
  sprintf(_getOutBuf(), err_msg, sensor);
  Serial.println(_getOutBuf());
}

/*
 * Internal helper to do shared checksum logic
 */
int _calculateChecksumValue(const char *sensor_name, int num_vals, va_list values) {
  int cs = 0;
  const char *c_ptr = sensor_name;

  for (int i = 0; i < num_vals; ++i) {
    cs += lround(va_arg(values, double));
  }

  while (*c_ptr != 0) {
    cs += *c_ptr++;
  }

  return cs;
}

/**
 * Calculates a checksum value for a given sensorName and value
 *
 * @param sensor_name name of sensor
 * @param num_vals Number of values
 * @param values variable number of floats to write
 *
 * @return checksum
 */
int calculateChecksum(const char *sensor_name, int num_vals, ...) {
  va_list values;
  va_start(values, num_vals);
  int cs = _calculateChecksumValue(sensor_name, num_vals, values);
  va_end(values);
  return cs;
}

/**
 * Create a CSV string with a generic array of float values and a sensor name. Optional timestamp
 * argument allows passing in a timestamp; will use millis() otherwise.
 *
 * @param sensorName string sensor name
 * @param timestamp optional timestamp. If 0, millis() will be called.
 * @param numValues number of float values
 * @param variable float values
 *
 * @return pointer to output buffer
 */
const char * valuesToCSV(const char *sensorName, unsigned long timestamp, int numValues, ...) {
  int i, name_len;
  va_list args;

  if (timestamp == 0) {
    timestamp = millis();
  }

  _resetOutBuf();

  ultoa(timestamp, _getOutBuf(), 10);
  _output_buf_len = strlen(_getOutBuf());

  if (sensorName != NULL) {
    if ((name_len = strlen(sensorName)) > 50) {
      name_len = 50;
    }
    _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
    memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
    _output_buf_len += name_len;
  }

  va_start(args, numValues);
  for (i = 0; i < numValues; ++i) {
    // We don't know *exactly* how long the floating point value is
    // going to be, so just take a guess here...
    if (_output_buf_len > OUTPUT_BUF_SIZE - 10) {
      break;
    }
    _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
    dtostrf(va_arg(args, double), 2, 3, _getOutBuf() + _output_buf_len);
    _output_buf_len = strlen(_getOutBuf());
  }
  va_end(args);

  if (_output_buf_len < OUTPUT_BUF_SIZE - 10) {
    va_start(args, numValues);
    int cs = _calculateChecksumValue(sensorName, numValues, args);
    va_end(args);
    _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
    itoa(cs, _getOutBuf() + _output_buf_len, 10);
    _output_buf_len = strlen(_getOutBuf());
  }
  _getOutBuf()[_output_buf_len++] = '\n';

  return _getOutBuf();
}

/**
 * Create a CSV string with a generic float value and a sensor name. Optional timestamp
 * argument allows passing in a timestamp; will use millis() otherwise.
 *
 * @param sensorName string sensor name
 * @param value value to write
 * @param timestamp optional timestamp. If 0, millis() will be called.
 *
 * @return pointer to output buffer
 */
const char * valueToCSV(const char *sensorName, float value, unsigned long timestamp) {
  return valuesToCSV(sensorName, timestamp, 1, value);
}

/*
 * Internal helper to build the JSON string at the proper point in the output buffer
 * with the correct values and labels
 */
int _writeJSONValue(char *buf, const char *sensor_name, const char *unit, float value) {
  char num [32];
  char format_str[80];
  // inexact estimate on the number of characters the value will take up...
  if (strlen(sensor_name) + strlen(unit) + 10 + _output_buf_len > OUTPUT_BUF_SIZE) {
    return -1;
  }

  dtostrf(value, 4, 2, num);
  strcpy_P(format_str, json_format);
  _output_buf_len += sprintf(buf, format_str,
                             JSON_PREFIX, sensor_name, unit, num,
                             calculateChecksum(sensor_name, 1, value), JSON_SUFFIX);
  return _output_buf_len;
}

/**
 * Create a JSON string with a generic array of float values and a sensor name.
 *
 * @param sensorName string sensor name
 * @param unit unit the sensor values are in
 * @param numValues number of pairs of string labels and float values
 * @param variable pairs of string labels and float values
 *
 * @return pointer to output buffer
 */
const char * valuesToJSON(const char *sensorName, unsigned char unit, int numValues, ...) {
  int i = 0;
  size_t nameLength = strlen(sensorName) + 1; // For null terminator
  va_list args;

  char concatBuf[5];
  strcpy_P(concatBuf, concat_string);

  _resetOutBuf();
  va_start(args, numValues);
  for (i = 0; i < numValues; ++i) {
    char * label = va_arg(args, char *);
    float value = va_arg(args, double);
    char nameBuf[nameLength + strlen(label) + 1];

    sprintf(nameBuf, concatBuf, sensorName, label); // concatBuf = "%s%s";
    _writeJSONValue(&_getOutBuf()[_output_buf_len], nameBuf, unit_to_str(unit), value);
  }
  va_end(args);

  return _getOutBuf();
}

/**
 * Creates a JSON string with the appropriate values
 *
 * @param sensorName the label to be given to some data
 * @param unit the unit of measurement used
 * @param value the sensor value
 *
 * @return the output buffer
 */
const char * valueToJSON(const char *sensorName, unsigned char unit, float value) {
  _resetOutBuf();
  _writeJSONValue(_getOutBuf(), sensorName, unit_to_str(unit), value);
  return _getOutBuf();
}

/*
 * Adds the sensor unit in parantheses to the outputbuffer
 */
void _bufSensorHeaderUnit(const char * unit) {
  _getOutBuf()[_output_buf_len++] = '(';
  memcpy(&(_getOutBuf()[_output_buf_len]), unit, strlen(unit));
  _output_buf_len += strlen(unit);
  _getOutBuf()[_output_buf_len++] = ')';
}


/**************************************************************************//**
 * @brief   Initializes the sensor with any set configurations
 * @ingroup sensor
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 *****************************************************************************/
boolean Sensor::begin(void) {
  catchSpaceboard();
  this->initialized = this->initialize();

  if (!this->initialized) {
    _writeErrorMessage(begin_error_msg, this->name);
  }

  return this->initialized;
}

/**
 * @brief   Makes sure the sensor was initialized then calls the sensor specific read
 * @ingroup sensor
 * @retval  true  A sensor reading was attempted
 * @retval  false No sensor reading was attempted. Must `begin()` first
 */
boolean Sensor::read(void) {
  if (this->initialized) {
    this->header.timestamp = millis();
    return this->readSensor();
  }

  return this->initialized;
}

/**
 * @brief   Takes a reading from the sensor and returns value in CSV format
 * @ingroup sensor
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Sensor::readToCSV(const char * sensorName) {
  this->read();
  return this->toCSV(sensorName);
}

/**
 * @brief   Takes a reading from the sensor and returns value in JSON format
 * @ingroup sensor
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Sensor::readToJSON(const char * sensorName) {
  this->read();
  return this->toJSON(sensorName);
}

/**
 * @brief   Initializes member variables for each sensor
 * @ingroup sensor
 * @param   sensor_id The type of sensor
 * @param   unit The unit the sensor values use
 * @param   name The default name the sensor uses
 *
 * @note    These values are not initialized as member variables in child class
 *          constructors because they are inherited from the base Sensor class
 *          and need a Sensor Constructor they can be passed to. However, this
 *          constructor causes more memory overhead than it was decided to be
 *          worth. Which is why they're explicitly set instead of initialized.
 */
void Sensor::initializeHeader(sensor_id_t sensor_id, data_unit_t unit, const char name[] PROGMEM) {
  this->name = name;
  this->header.sensor_id = sensor_id;
  this->header.unit = unit;
  this->header.timestamp = 0;
  this->initialized = false;
}


/**************************************************************************//**
 * @brief   Constructs Acceleration sensor object
 * @ingroup acceleration
 *
 * Example Usage:
 * @code
 *     Acceleration accel;                        // Instantiate sensor object
 *     accel.begin();                             // Initialize sensor
 *     Serial.println(accel.readToJSON("accel")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
Acceleration::Acceleration(void) :
  gGain(LSM303_ACCEL_GAIN8G)
{
  this->initializeHeader(SENSORID_ADAFRUIT9DOFIMU, DATA_UNIT_METER_PER_SECONDSQUARED, acceleration_sensor_name);
}

/**
 * @brief   Sets the gain configuration variable and constructs an object
 * @ingroup acceleration
 *
 * @param gain Advanced configuration for accelerometer's gain
 *     - LSM303_ACCEL_GAIN2G
 *     - LSM303_ACCEL_GAIN4G
 *     - LSM303_ACCEL_GAIN6G
 *     - LSM303_ACCEL_GAIN8G (Default)
 *     - LSM303_ACCEL_GAIN16G
 *
 * Example Usage:
 * @code
 *     Acceleration accel(LSM303_ACCEL_GAIN2G);   // Instantiate sensor object
 *     accel.begin();                             // Initialize sensor
 *     Serial.println(accel.readToJSON("accel")); // Read and print values in JSON
 * @endcode
 */
Acceleration::Acceleration(lsm303_accel_gain_e gain) :
  gGain(gain)
{
  this->initializeHeader(SENSORID_ADAFRUIT9DOFIMU, DATA_UNIT_METER_PER_SECONDSQUARED, acceleration_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup acceleration
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean Acceleration::initialize(void) {
  return lsm303_accel_init(this->gGain);
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup acceleration
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean Acceleration::readSensor(void) {
  lsm303_getAccel(&(this->x), &(this->y), &(this->z));
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int Acceleration::_bufCSVValues(void) {
  dtostrf(this->x, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->y, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->z, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 3, this->x, this->y, this->z);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void Acceleration::_bufCSVHeaders(void) {
  char sensorName[25];
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, acceleration_sensor_name);
  int name_len = strlen(sensorName);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'X';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'Y';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'Z';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup acceleration
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Acceleration::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToCSV(sensorName, this->header.timestamp, 3,
                       this->x, this->y, this->z);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup acceleration
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Acceleration::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToJSON(sensorName, this->header.unit, 3, "X", this->x,
                        "Y", this->y, "Z", this->z);
  } else {
    return "";
  }
}


/**************************************************************************//**
 * @brief   Constructs Gyro sensor object
 * @ingroup gyro
 *
 * Example Usage:
 * @code
 *     Gyro gyro;                               // Instantiate sensor object
 *     gyro.begin();                            // Initialize sensor
 *     Serial.println(gyro.readToJSON("gyro")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
Gyro::Gyro(void) :
  range(0x20)
{
  this->initializeHeader(SENSORID_ADAFRUIT9DOFIMU, DATA_UNIT_RADIAN_PER_SECOND, gyro_sensor_name);
}

/**
 * @brief   Sets the range configuration variable and constructs an object
 * @ingroup gyro
 *
 * @param range Advanced configuration for gyro's range
 *     - 0x00  (SENSITIVITY_250DPS)
 *     - 0x10  (SENSITIVITY_500DPS)
 *     - 0x20  (SENSITIVITY_2000DPS) (Default)
 *
 * Example Usage:
 * @code
 *     Gyro gyro(0x00);                         // Instantiate sensor object
 *     gyro.begin();                            // Initialize sensor
 *     Serial.println(gyro.readToJSON("gyro")); // Read and print values in JSON
 * @endcode
 */
Gyro::Gyro(uint8_t range) :
  range(range)
{
  this->initializeHeader(SENSORID_ADAFRUIT9DOFIMU, DATA_UNIT_RADIAN_PER_SECOND, gyro_sensor_name);
}

/**
 * @brief   Initializes the sensor with advanced configurations or defaults
 * @ingroup gyro
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean Gyro::initialize(void) {
  return l3gd20h_init(this->range);
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup gyro
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean Gyro::readSensor(void) {
  l3gd20h_getOrientation(&(this->x), &(this->y), &(this->z));
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int Gyro::_bufCSVValues(void) {
  dtostrf(this->x, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->y, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->z, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 3, this->x, this->y, this->z);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void Gyro::_bufCSVHeaders(void) {
  char sensorName[25];
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, gyro_sensor_name);
  int name_len = strlen(sensorName);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'X';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'Y';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'Z';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup gyro
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Gyro::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToCSV(sensorName, this->header.timestamp, 3,
                       this->x, this->y, this->z);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup gyro
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Gyro::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToJSON(sensorName, this->header.unit, 3, "X", this->x,
                        "Y", this->y, "Z", this->z);
  } else {
    return "";
  }
}


/**************************************************************************//**
 * @brief   Constructs Luminosity sensor object
 * @ingroup luminosity
 *
 * Example Usage:
 * @code
 *     Luminosity lum;                        // Instantiate sensor object
 *     lum.begin();                           // Initialize sensor
 *     Serial.println(lum.readToJSON("lum")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
Luminosity::Luminosity(void) :
  gain(TSL2561_GAIN_1X),
  intTime(TSL2561_INTEGRATIONTIME_13MS)
{
  this->initializeHeader(SENSORID_TSL2561, DATA_UNIT_LUX, luminosity_sensor_name);
}

/**
 * @brief   Sets the integration time and gain configuration variables and constructs an object
 * @ingroup luminosity
 *
 * @param intTime Advanced configuration for TSL2561 integration time
 *     - TSL2561_INTEGRATIONTIME_13MS (Default)
 *     - TSL2561_INTEGRATIONTIME_101MS
 *     - TSL2561_INTEGRATIONTIME_402MS
 * @param gain Advanced configuration for TSL2561 gain
 *     - TSL2561_GAIN_1X (Default)
 *     - TSL2561_GAIN_16X
 *
 * Example Usage:
 * @code
 *     Luminosity lum(TSL2561_INTEGRATIONTIME_101MS, TSL2561_GAIN_16X);
 *     lum.begin();                           // Initialize sensor
 *     Serial.println(lum.readToJSON("lum")); // Read and print values in JSON
 * @endcode
 */
Luminosity::Luminosity(tsl2561IntegrationTime_t intTime, tsl2561Gain_t gain) :
  gain(gain),
  intTime(intTime)
{
  this->initializeHeader(SENSORID_TSL2561, DATA_UNIT_LUX, luminosity_sensor_name);
}

/**
 * @brief   Sets the gain and integration time configuration variables and constructs an object
 * @ingroup luminosity
 *
 * @param gain Advanced configuration for TSL2561 gain
 *     - TSL2561_GAIN_1X (Default)
 *     - TSL2561_GAIN_16X
 * @param intTime Advanced configuration for TSL2561 integration time
 *     - TSL2561_INTEGRATIONTIME_13MS (Default)
 *     - TSL2561_INTEGRATIONTIME_101MS
 *     - TSL2561_INTEGRATIONTIME_402MS
 *
 * Example Usage:
 * @code
 *     Luminosity lum(TSL2561_GAIN_16X, TSL2561_INTEGRATIONTIME_101MS);
 *     lum.begin();                           // Initialize sensor
 *     Serial.println(lum.readToJSON("lum")); // Read and print values in JSON
 * @endcode
 */
Luminosity::Luminosity(tsl2561Gain_t gain, tsl2561IntegrationTime_t intTime) :
  gain(gain),
  intTime(intTime)
{
  this->initializeHeader(SENSORID_TSL2561, DATA_UNIT_LUX, luminosity_sensor_name);
}

/**
 * @brief   Sets the integration time configuration variable and constructs an object
 * @ingroup luminosity
 *
 * @param intTime Advanced configuration for TSL2561 integration time
 *     - TSL2561_INTEGRATIONTIME_13MS (Default)
 *     - TSL2561_INTEGRATIONTIME_101MS
 *     - TSL2561_INTEGRATIONTIME_402MS
 *
 * Example Usage:
 * @code
 *     Luminosity lum(TSL2561_INTEGRATIONTIME_101MS); // Instantiate sensor object
 *     lum.begin();                                   // Initialize sensor
 *     Serial.println(lum.readToJSON("lum"));         // Read and print values in JSON
 * @endcode
 */
Luminosity::Luminosity(tsl2561IntegrationTime_t intTime) :
  gain(TSL2561_GAIN_1X),
  intTime(intTime)
{
  this->initializeHeader(SENSORID_TSL2561, DATA_UNIT_LUX, luminosity_sensor_name);
}

/**
 * @brief   Sets the gain configuration variable and constructs an object
 * @ingroup luminosity
 *
 * @param gain Advanced configuration for TSL2561 gain
 *     - TSL2561_GAIN_1X (Default)
 *     - TSL2561_GAIN_16X
 *
 * Example Usage:
 * @code
 *     Luminosity lum(TSL2561_GAIN_16X);      // Instantiate sensor object
 *     lum.begin();                           // Initialize sensor
 *     Serial.println(lum.readToJSON("lum")); // Read and print values in JSON
 * @endcode
 */
Luminosity::Luminosity(tsl2561Gain_t gain) :
  gain(gain),
  intTime(TSL2561_INTEGRATIONTIME_13MS)
{
  this->initializeHeader(SENSORID_TSL2561, DATA_UNIT_LUX, luminosity_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup luminosity
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean Luminosity::initialize(void) {
  return tsl2561_init(this->intTime, this->gain);
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup luminosity
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean Luminosity::readSensor(void) {
  this->lux = tsl2561_getLux();
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int Luminosity::_bufCSVValues(void) {
  dtostrf(this->lux, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 1, this->lux);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void Luminosity::_bufCSVHeaders(void) {
  char sensorName[25];
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, luminosity_sensor_name);
  int name_len = strlen(sensorName);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup luminosity
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Luminosity::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToCSV(sensorName, this->lux, this->header.timestamp);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup luminosity
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Luminosity::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToJSON(sensorName, this->header.unit, this->lux);
  } else {
    return "";
  }
}


/**************************************************************************//**
 * @brief   Constructs Magnetic sensor object
 * @ingroup magnetic
 *
 * Example Usage:
 * @code
 *     Magnetic mag;                          // Instantiate sensor object
 *     mag.begin();                           // Initialize sensor
 *     Serial.println(mag.readToJSON("mag")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
Magnetic::Magnetic(void) :
  gaussScale(LSM303_MAG_SCALE4GAUSS)
{
  this->initializeHeader(SENSORID_ADAFRUIT9DOFIMU, DATA_UNIT_MICROTESLA, magnetic_sensor_name);
}

/**
 * @brief   Sets the gauss scale configuration variable and constructs an object
 * @ingroup magnetic
 *
 * @param gaussScale Advanced configuration for magnetometer's scale
 *     - LSM303_MAG_SCALE1_3GAUSS
 *     - LSM303_MAG_SCALE2GAUSS
 *     - LSM303_MAG_SCALE2_5GAUSS
 *     - LSM303_MAG_SCALE4GAUSS (Default)
 *     - LSM303_MAG_SCALE4_7GAUSS
 *     - LSM303_MAG_SCALE5_6GAUSS
 *     - LSM303_MAG_SCALE8GAUSS
 *     - LSM303_MAG_SCALE12GAUSS
 *
 * Example Usage:
 * @code
 *     Magnetic mag(LSM303_MAG_SCALE8GAUSS);  // Instantiate sensor object
 *     mag.begin();                           // Initialize sensor
 *     Serial.println(mag.readToJSON("mag")); // Read and print values in JSON
 * @endcode
 */
Magnetic::Magnetic(lsm303_mag_scale_e gaussScale) :
  gaussScale(gaussScale)
{
  this->initializeHeader(SENSORID_ADAFRUIT9DOFIMU, DATA_UNIT_MICROTESLA, magnetic_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup magnetic
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean Magnetic::initialize(void) {
  return lsm303_mag_init(this->gaussScale);
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup magnetic
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean Magnetic::readSensor(void) {
  lsm303_getMag(&(this->x), &(this->y), &(this->z));
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int Magnetic::_bufCSVValues(void) {
  dtostrf(this->x, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->y, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->z, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 3, this->x, this->y, this->z);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void Magnetic::_bufCSVHeaders(void) {
  char sensorName[25];
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, magnetic_sensor_name);
  int name_len = strlen(sensorName);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'X';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'Y';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _getOutBuf()[_output_buf_len++] = 'Z';
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup magnetic
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Magnetic::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToCSV(sensorName, this->header.timestamp, 3,
                       this->x, this->y, this->z);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup magnetic
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Magnetic::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToJSON(sensorName, this->header.unit, 3, "X", this->x,
                        "Y", this->y, "Z", this->z);
  } else {
    return "";
  }
}


/**************************************************************************//**
 * @brief   Constructs Orientation calculation object using provided Acceleration and Magnetic objects
 * @ingroup orientation
 *
 * Example Usage:
 * @code
 *     Acceleration accel;
 *     Magnetic mag;
 *     Orientation orient(accel, mag);                   // Instantiate sensor object
 *     orient.begin();                                   // Initialize sensor
 *     Serial.println(orient.readToJSON("orientation")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
Orientation::Orientation(Acceleration & accel, Magnetic & mag) :
  accel(&accel),
  mag(&mag)
{
  this->initializeHeader(SENSORID_ADAFRUIT9DOFIMU, DATA_UNIT_DEGREES, orientation_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup orientation
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean Orientation::initialize(void) {
  return accel->initialized && mag->initialized;
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup orientation
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean Orientation::readSensor(void) {
  this->accel->read();
  this->mag->read();

  float roll;
  float pitch;
  float heading;
  const float PI_F = 3.141592653F;

  // Roll is rotation around x-axis (-180 <= roll <= 180)
  // Positive roll is clockwise rotation wrt positive x axis
  roll = (float) atan2(this->accel->y, this->accel->z);

  // Pitch is rotation around y-axis (-180 <= pitch <= 180)
  // Positive pitch is clockwise rotation wrt positive y axis
  if (this->accel->y * sin(roll) + this->accel->z * cos(roll) == 0) {
    pitch = this->accel->x > 0 ? (PI_F / 2) : (-PI_F / 2);
  } else {
    pitch = (float)atan(-this->accel->x / (this->accel->y * sin(roll) + this->accel->z * cos(roll)));
  }

  // Heading is rotation around z-axis
  // Positive heading is clockwise rotation wrt positive z axis
  heading = (float)atan2(this->mag->z * sin(roll) - this->mag->y * cos(roll),
                         this->mag->x * cos(pitch) + this->mag->y * sin(pitch) * sin(roll) +
                         this->mag->z * sin(pitch) * cos(roll));

  // Convert radians to degrees
  this->roll = roll * 180 / PI_F;
  this->pitch = pitch * 180 / PI_F;
  this->heading = heading * 180 / PI_F;

  this->header.timestamp = max(this->accel->header.timestamp, this->mag->header.timestamp);
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int Orientation::_bufCSVValues(void) {
  dtostrf(this->roll, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->pitch, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->heading, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 3, this->roll, this->pitch, this->heading);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void Orientation::_bufCSVHeaders(void) {
  char sensorName[25]; // "orientation"
  char sensorRoll[5];  // "Roll"
  char sensorPitch[6]; // "Pitch"
  char sensorHead[8];  // "Heading"
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, orientation_sensor_name);
  strcpy_P(sensorRoll, orientation_sensor_roll);
  strcpy_P(sensorPitch, orientation_sensor_pitch);
  strcpy_P(sensorHead, orientation_sensor_heading);

  int name_len = strlen(sensorName);
  int roll_len = strlen(sensorRoll);
  int pitch_len = strlen(sensorPitch);
  int head_len = strlen(sensorHead);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorRoll, roll_len);
  _output_buf_len += roll_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorPitch, pitch_len);
  _output_buf_len += pitch_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorHead, head_len);
  _output_buf_len += head_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup orientation
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Orientation::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToCSV(sensorName, this->header.timestamp, 3,
                       this->roll, this->pitch, this->heading);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup orientation
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Orientation::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToJSON(sensorName, this->header.unit, 3, "Roll", this->roll,
                        "Pitch", this->pitch, "Heading", this->heading);
  } else {
    return "";
  }
}


/**************************************************************************//**
 * @brief   Constructs Pressure object
 * @ingroup pressure
 *
 * Example Usage:
 * @code
 *     Pressure press;                               // Instantiate sensor object
 *     press.begin();                                // Initialize sensor
 *     Serial.println(press.readToJSON("pressure")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
Pressure::Pressure(void) :
  bmp085_mode(BMP085_MODE_ULTRAHIGHRES)
{
  this->initializeHeader(SENSORID_BMP180, DATA_UNIT_HECTOPASCAL, pressure_sensor_name);
}

/**
 * @brief   Sets the resolution mode configuration variable and constructs an object
 * @ingroup pressure
 *
 * @param mode Advanced configuration for BMP180's resolution mode
 *     - BMP085_MODE_ULTRALOWPOWER
 *     - BMP085_MODE_STANDARD
 *     - BMP085_MODE_HIGHRES
 *     - BMP085_MODE_ULTRAHIGHRES (Default)
 *
 * Example Usage:
 * @code
 *     Pressure press(BMP085_MODE_ULTRALOWPOWER);    // Instantiate sensor object
 *     press.begin();                                // Initialize sensor
 *     Serial.println(press.readToJSON("pressure")); // Read and print values in JSON
 * @endcode
 */
Pressure::Pressure(bmp085_mode_t mode) :
  bmp085_mode(mode)
{
  this->initializeHeader(SENSORID_BMP180, DATA_UNIT_HECTOPASCAL, pressure_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup pressure
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean Pressure::initialize(void) {
  if (ARDUSAT_SPACEBOARD) {
    _writeErrorMessage(unavailable_on_hardware_error_msg, pressure_sensor_name, spaceboard_hardware_name);
  }

  return bmp180_init(this->bmp085_mode);
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup pressure
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean Pressure::readSensor(void) {
  bmp180_getPressure(&(this->pressure));
  return true;
}

/**
 * @brief Calculates current altitude given pressure reading and provided pressure at sea level
 *
 * Equation taken from BMP180 datasheet (page 16):
 * http://www.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf
 *
 * Note that using the equation from wikipedia can give bad results at high
 * altitude. See this thread for more information:
 * http://forums.adafruit.com/viewtopic.php?f=22&t=58064
 *
 * @ingroup pressure
 * @param   seaLevelPressure Known pressure at sea level in hPa
 * @return  calculated altitude in meters
 */
float Pressure::altitudeFromSeaLevelPressure(float seaLevelPressure) {
  return 44330.0 * (1.0 - pow(this->pressure / seaLevelPressure, 0.1903));
}

/**
 * @brief Calculate the pressure at sea level given current altitude and pressure reading
 *
 * Equation taken from BMP180 datasheet (page 17):
 * http://www.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf
 *
 * Note that using the equation from wikipedia can give bad results at high
 * altitude. See this thread for more information:
 * http://forums.adafruit.com/viewtopic.php?f=22&t=58064
 *
 * @ingroup pressure
 * @param   altitude Known altitude in meters
 * @return  calculated pressure at sea level in hPa
 */
float Pressure::seaLevelPressureFromAltitude(float altitude) {
  return this->pressure / pow(1.0 - (altitude / 44330.0), 5.255);
}

/*
 * Adds the sensor values into the output buffer
 */
int Pressure::_bufCSVValues(void) {
  dtostrf(this->pressure, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 1, this->pressure);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void Pressure::_bufCSVHeaders(void) {
  char sensorName[25];
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, pressure_sensor_name);
  int name_len = strlen(sensorName);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup pressure
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Pressure::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToCSV(sensorName, this->pressure, this->header.timestamp);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup pressure
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Pressure::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToJSON(sensorName, this->header.unit, this->pressure);
  } else {
    return "";
  }
}


/**************************************************************************//**
 * @brief   Constructs RGBLight sensor object, default uses TCS34725 sensor
 * @ingroup rgblight
 *
 * Example Usage:
 * @code
 *     RGBLight rgb;                          // Instantiate sensor object
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
RGBLight::RGBLight(void) :
  tcsIt(TCS34725_INTEGRATIONTIME_154MS),
  tcsGain(TCS34725_GAIN_1X)
{
  this->initializeHeader(SENSORID_TCS34725, DATA_UNIT_NONE, rgblight_sensor_name);
}

/**
 * @brief   Protected constructor used by public constructors
 * @ingroup rgblight
 */
RGBLight::RGBLight(tcs34725IntegrationTime_t tcsIt, tcs34725Gain_t tcsGain) :
  tcsIt(tcsIt),
  tcsGain(tcsGain)
{
  this->initializeHeader(SENSORID_TCS34725, DATA_UNIT_NONE, rgblight_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup rgblight
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean RGBLight::initialize(void) {
  if (!ARDUSAT_SPACEBOARD) {
    _writeErrorMessage(unavailable_on_hardware_error_msg, rgblight_sensor_name, spacekit_hardware_name);
  }

  return tcs34725_init(tcsIt, tcsGain);
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup rgblight
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean RGBLight::readSensor(void) {
  tcs34725_getRGB(&(this->red), &(this->green), &(this->blue));
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int RGBLight::_bufCSVValues(void) {
  dtostrf(this->red, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->green, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  dtostrf(this->blue, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 3, this->red, this->green, this->blue);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void RGBLight::_bufCSVHeaders(void) {
  char sensorName[25]; // "rgb"
  char sensorRed[4];   // "Red"
  char sensorGreen[6]; // "Green"
  char sensorBlue[5];  // "Blue"
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, rgblight_sensor_name);
  strcpy_P(sensorRed, rgblight_sensor_red);
  strcpy_P(sensorGreen, rgblight_sensor_green);
  strcpy_P(sensorBlue, rgblight_sensor_blue);

  int name_len = strlen(sensorName);
  int red_len = strlen(sensorRed);
  int green_len = strlen(sensorGreen);
  int blue_len = strlen(sensorBlue);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorRed, red_len);
  _output_buf_len += red_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorGreen, green_len);
  _output_buf_len += green_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));

  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorBlue, blue_len);
  _output_buf_len += blue_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup rgblight
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * RGBLight::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToCSV(sensorName, this->header.timestamp, 3,
                       this->red, this->green, this->blue);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup rgblight
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * RGBLight::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valuesToJSON(sensorName, this->header.unit, 3, "Red", this->red,
                        "Green", this->green, "Blue", this->blue);
  } else {
    return "";
  }
}

/**
 * @brief   Constructs TCS34725 RGBLight sensor object
 * @ingroup rgblight
 *
 * Example Usage:
 * @code
 *     RGBLightTCS rgb;                       // Instantiate sensor object
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 */
RGBLightTCS::RGBLightTCS(void) :
  RGBLight(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_1X)
{
}

/**
 * @brief   Sets the integration time and gain configuration variables and constructs an object
 * @ingroup rgblight
 *
 * @param tcsIt Advanced configuration for TCS34725 integration time
 *     - TCS34725_INTEGRATIONTIME_2_4MS
 *     - TCS34725_INTEGRATIONTIME_24MS
 *     - TCS34725_INTEGRATIONTIME_50MS
 *     - TCS34725_INTEGRATIONTIME_101MS
 *     - TCS34725_INTEGRATIONTIME_154MS (Default)
 *     - TCS34725_INTEGRATIONTIME_700MS
 * @param tcsGain Advanced configuration for TCS34725 gain
 *     - TCS34725_GAIN_1X (Default)
 *     - TCS34725_GAIN_4X
 *     - TCS34725_GAIN_16X
 *     - TCS34725_GAIN_60X
 *
 * Example Usage:
 * @code
 *     RGBLightTCS rgb(TCS34725_INTEGRATIONTIME_101MS, TCS34725_GAIN_4X);
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 */
RGBLightTCS::RGBLightTCS(tcs34725IntegrationTime_t tcsIt, tcs34725Gain_t tcsGain) :
  RGBLight(tcsIt, tcsGain)
{
}

/**
 * @brief   Sets the gain and integration time configuration variables and constructs an object
 * @ingroup rgblight
 *
 * @param tcsGain Advanced configuration for TCS34725 gain
 *     - TCS34725_GAIN_1X (Default)
 *     - TCS34725_GAIN_4X
 *     - TCS34725_GAIN_16X
 *     - TCS34725_GAIN_60X
 * @param tcsIt Advanced configuration for TCS34725 integration time
 *     - TCS34725_INTEGRATIONTIME_2_4MS
 *     - TCS34725_INTEGRATIONTIME_24MS
 *     - TCS34725_INTEGRATIONTIME_50MS
 *     - TCS34725_INTEGRATIONTIME_101MS
 *     - TCS34725_INTEGRATIONTIME_154MS (Default)
 *     - TCS34725_INTEGRATIONTIME_700MS
 *
 * Example Usage:
 * @code
 *     RGBLightTCS rgb(TCS34725_GAIN_4X, TCS34725_INTEGRATIONTIME_101MS);
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 */
RGBLightTCS::RGBLightTCS(tcs34725Gain_t tcsGain, tcs34725IntegrationTime_t tcsIt) :
  RGBLight(tcsIt, tcsGain)
{
}

/**
 * @brief   Sets the integration time configuration variable and constructs an object
 * @ingroup rgblight
 *
 * @param tcsIt Advanced configuration for TCS34725 integration time
 *     - TCS34725_INTEGRATIONTIME_2_4MS
 *     - TCS34725_INTEGRATIONTIME_24MS
 *     - TCS34725_INTEGRATIONTIME_50MS
 *     - TCS34725_INTEGRATIONTIME_101MS
 *     - TCS34725_INTEGRATIONTIME_154MS (Default)
 *     - TCS34725_INTEGRATIONTIME_700MS
 *
 * Example Usage:
 * @code
 *     RGBLightTCS rgb(TCS34725_INTEGRATIONTIME_101MS);
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 */
RGBLightTCS::RGBLightTCS(tcs34725IntegrationTime_t tcsIt) :
  RGBLight(tcsIt, TCS34725_GAIN_1X)
{
}

/**
 * @brief   Sets the gain configuration variable and constructs an object
 * @ingroup rgblight
 *
 * @param tcsGain Advanced configuration for TCS34725 gain
 *     - TCS34725_GAIN_1X (Default)
 *     - TCS34725_GAIN_4X
 *     - TCS34725_GAIN_16X
 *     - TCS34725_GAIN_60X
 *
 * Example Usage:
 * @code
 *     RGBLightTCS rgb(TCS34725_GAIN_4X);
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 */
RGBLightTCS::RGBLightTCS(tcs34725Gain_t tcsGain) :
  RGBLight(TCS34725_INTEGRATIONTIME_154MS, tcsGain)
{
}

/**
 * @brief   Constructs ISL29125 RGBLight sensor object
 * @ingroup rgblight
 *
 * Example Usage:
 * @code
 *     RGBLightISL rgb;                       // Instantiate sensor object
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 */
RGBLightISL::RGBLightISL(void) :
  islIntensity(CFG1_10KLUX)
{
  this->initializeHeader(SENSORID_ISL29125, DATA_UNIT_NONE, rgblight_sensor_name);
}

/**
 * @brief   Sets the intensity configuration variable and constructs an object
 * @ingroup rgblight
 *
 * @param islIntensity Advanced configuration for ISL29125 intensity
 *     - CFG1_375LUX
 *     - CFG1_10KLUX (Default)
 *
 * Example Usage:
 * @code
 *     RGBLightISL rgb(CSG1_375LUX);
 *     rgb.begin();                           // Initialize sensor
 *     Serial.println(rgb.readToJSON("rgb")); // Read and print values in JSON
 * @endcode
 */
RGBLightISL::RGBLightISL(uint8_t islIntensity) :
  islIntensity(islIntensity)
{
  this->initializeHeader(SENSORID_ISL29125, DATA_UNIT_NONE, rgblight_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup rgblight
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean RGBLightISL::initialize(void) {
  if (!ARDUSAT_SPACEBOARD) {
    _writeErrorMessage(unavailable_on_hardware_error_msg, rgblight_sensor_name, spacekit_hardware_name);
  }

  return isl29125_init(islIntensity);
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup rgblight
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean RGBLightISL::readSensor(void) {
  isl29125_getRGB(&(this->red), &(this->green), &(this->blue));
  return true;
}


/**************************************************************************//**
 * @brief   Constructs Temperature sensor object, default uses TMP102 sensor
 * @ingroup temperature
 *
 * @note    TMP102 is an ambient temperature sensor and MLX90614 is an infrared
 *          temperature sensor
 *
 * Example Usage:
 * @code
 *     Temperature temp;                                // Instantiate sensor object
 *     temp.begin();                                    // Initialize sensor
 *     Serial.println(temp.readToJSON("ambient_temp")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
Temperature::Temperature(void) {
  this->initializeHeader(SENSORID_TMP102, DATA_UNIT_DEGREES_CELSIUS, temperature_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup temperature
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean Temperature::initialize(void) {
  return tmp102_init();
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup temperature
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean Temperature::readSensor(void) {
  this->t = tmp102_getTempCelsius();
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int Temperature::_bufCSVValues(void) {
  dtostrf(this->t, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 1, this->t);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void Temperature::_bufCSVHeaders(void) {
  char sensorName[25];
  const char * unit = unit_to_str(this->header.unit);
  if (this->header.sensor_id == SENSORID_TMP102) {
    strcpy_P(sensorName, temperature_sensor_name);
  } else {
    strcpy_P(sensorName, irtemperature_sensor_name);
  }
  int name_len = strlen(sensorName);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup temperature
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * Temperature::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToCSV(sensorName, this->t, this->header.timestamp);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup temperature
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * Temperature::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToJSON(sensorName, this->header.unit, this->t);
  } else {
    return "";
  }
}

/**
 * @brief   Constructs MLX90614 infrared Temperature sensor object
 * @ingroup temperature
 *
 * Example Usage:
 * @code
 *     TemperatureMLX temp;                              // Instantiate sensor object
 *     temp.begin();                                     // Initialize sensor
 *     Serial.println(temp.readToJSON("infrared_temp")); // Read and print values in JSON
 * @endcode
 */
TemperatureMLX::TemperatureMLX(void) {
  this->initializeHeader(SENSORID_MLX90614, DATA_UNIT_DEGREES_CELSIUS, irtemperature_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup temperature
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean TemperatureMLX::initialize(void) {
  return mlx90614_init();
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup temperature
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean TemperatureMLX::readSensor(void) {
  this->t = mlx90614_getTempCelsius();
  return true;
}


/**************************************************************************//**
 * @brief   Constructs UVLight sensor object, default uses ML8511 sensor
 * @ingroup uvlight
 *
 * Example Usage:
 * @code
 *     UVLight uv;                          // Instantiate sensor object
 *     uv.begin();                          // Initialize sensor
 *     Serial.println(uv.readToJSON("uv")); // Read and print values in JSON
 * @endcode
 *****************************************************************************/
UVLight::UVLight(void) :
  ML8511_pin(DRIVER_ML8511_UV_PIN)
{
  this->initializeHeader(SENSORID_ML8511, DATA_UNIT_MILLIWATT_PER_CMSQUARED, uvlight_sensor_name);
}

/**
 * @brief   Protected constructor used by public constructors
 * @ingroup uvlight
 */
UVLight::UVLight(int pin) :
  ML8511_pin(pin)
{
  this->initializeHeader(SENSORID_ML8511, DATA_UNIT_MILLIWATT_PER_CMSQUARED, uvlight_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup uvlight
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean UVLight::initialize(void) {
  return ml8511_init();
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup uvlight
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean UVLight::readSensor(void) {
  this->uvindex = ml8511_getUV(this->ML8511_pin);
  return true;
}

/*
 * Adds the sensor values into the output buffer
 */
int UVLight::_bufCSVValues(void) {
  dtostrf(this->uvindex, 2, 3, _getOutBuf() + _output_buf_len);
  _output_buf_len = strlen(_getOutBuf());

  return calculateChecksum(0, 1, this->uvindex);
}

/*
 * Adds information necessary for a proper CSV Header into the output buffer
 */
void UVLight::_bufCSVHeaders(void) {
  char sensorName[25];
  const char * unit = unit_to_str(this->header.unit);
  strcpy_P(sensorName, uvlight_sensor_name);
  int name_len = strlen(sensorName);

  // Write out the default sensor name
  memcpy(&(_getOutBuf()[_output_buf_len]), sensorName, name_len);
  _output_buf_len += name_len;
  _bufSensorHeaderUnit(unit_to_str(this->header.unit));
}

/**
 * @brief   Returns last read value in CSV format
 * @ingroup uvlight
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in CSV format or empty string if uninitialized
 */
const char * UVLight::toCSV(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToCSV(sensorName, this->uvindex, this->header.timestamp);
  } else {
    return "";
  }
}

/**
 * @brief   Returns last read value in JSON format
 * @ingroup uvlight
 * @param   sensorName The text to display next to the value
 * @return  sensor readings in JSON format or empty string if uninitialized
 */
const char * UVLight::toJSON(const char * sensorName) {
  if (this->header.timestamp != 0) {
    return valueToJSON(sensorName, this->header.unit, this->uvindex);
  } else {
    return "";
  }
}

/**
 * @brief   Constructs ML8511 UVLight sensor object
 * @ingroup uvlight
 *
 * Example Usage:
 * @code
 *     UVLightML uv;                        // Instantiate sensor object
 *     uv.begin();                          // Initialize sensor
 *     Serial.println(uv.readToJSON("uv")); // Read and print values in JSON
 * @endcode
 */
UVLightML::UVLightML(void) :
  UVLight(DRIVER_ML8511_UV_PIN)
{
}

/**
 * @brief   Sets the analog pin configuration variable and constructs an object
 * @ingroup uvlight
 *
 * @param pin The analog pin the ML8511 board uses on the Arduino
 *     - DRIVER_ML8511_UV_PIN (A0) (Default)
 *
 * Example Usage:
 * @code
 *     UVLightML uv(A1);                    // Instantiate sensor object
 *     uv.begin();                          // Initialize sensor
 *     Serial.println(uv.readToJSON("uv")); // Read and print values in JSON
 * @endcode
 */
UVLightML::UVLightML(int pin) :
  UVLight(pin)
{
}

/**
 * @brief   Constructs SI1132 UVLight sensor object
 * @ingroup uvlight
 *
 * Example Usage:
 * @code
 *     UVLightSI uv;                        // Instantiate sensor object
 *     uv.begin();                          // Initialize sensor
 *     Serial.println(uv.readToJSON("uv")); // Read and print values in JSON
 * @endcode
 */
UVLightSI::UVLightSI(void) {
  this->initializeHeader(SENSORID_SI1132, DATA_UNIT_MILLIWATT_PER_CMSQUARED, uvlight_sensor_name);
}

/**
 * @brief   Initializes the sensor with any set configurations or defaults
 * @ingroup uvlight
 *
 * @retval true  Successfully initialized
 * @retval false Failed to initialize
 */
boolean UVLightSI::initialize(void) {
  return si1132_init();
}

/**
 * @brief   Takes a reading from the sensor
 * @ingroup uvlight
 *
 * @retval true  Successfully read
 * @retval false Failed to read
 */
boolean UVLightSI::readSensor(void) {
  this->uvindex = si1132_getUVIndex();
  return true;
}


/**************************************************************************//**
 * @brief   Constructs CSVWriter object
 * @ingroup csvwriter
 * @param   serialConnection Ardusat Serial connection writing object
 *****************************************************************************/
CSVWriter::CSVWriter(ArdusatSerial & serialConnection) :
  serialConnection(&serialConnection),
  includeChecksum(false),
  startTime(0),
  numSensors(0)
{
}

/**
 * @brief   Constructs CSVWriter object
 * @ingroup csvwriter
 * @param   serialConnection Ardusat Serial connection writing object
 * @param   includeChecksum Whether or not a checksum should be included with values
 */
CSVWriter::CSVWriter(ArdusatSerial & serialConnection, boolean includeChecksum) :
  serialConnection(&serialConnection),
  includeChecksum(includeChecksum),
  numSensors(0)
{
}

/**
 * @brief   Registers the CSVWriter object to use the provided sensor when logging values
 * @ingroup csvwriter
 * @param   sensor Sensor to be tracked by CSVWriter object
 * @retval  true The sensor is succesfully tracked by the CSVWriter
 * @retval  false There are too many sensors trying to be tracked by the CSVWriter
 */
boolean CSVWriter::registerSensor(Sensor & sensor) {
  if (this->numSensors >= MAX_SENSORS) {
    return false;
  }

  this->sensors[this->numSensors++] = &sensor;
  return true;
}

/**
 * @brief   Sets the initial logging start time to be added to all subsequent timestamps
 * @ingroup csvwriter
 * @param   startTime The time in milliseconds that the logging is expected to begin
 * @retval  true Succesful setting of the start time
 * @retval  false Error setting the start time
 */
boolean CSVWriter::setStartTime(unsigned long startTime) {
  this->startTime = startTime;
  return true;
}

/**
 * @brief   Builds a header to print at the top of a CSV file
 * @ingroup csvwriter
 * @return  string in the format of "timestamp(unit),sensor_name(unit),...,checksum"
 */
void CSVWriter::serialPrintHeader(void) {
  int i = 0;
  char tempBuf[30];
  Sensor * sensor;

  _resetOutBuf(); // Flush the buffer

  // Add 'timestamp(millis),' to beginning of row
  strcpy_P(tempBuf, CSV_TIMESTAMP);
  memcpy(&(_getOutBuf()[_output_buf_len]), tempBuf, strlen(tempBuf));
  _output_buf_len += strlen(tempBuf);
  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;

  // Add 'values(unit),...' for each sensor value to the row
  for (i = 0; i < this->numSensors; i++) {
    sensor = this->sensors[i];
    if (sensor->initialized) {
      sensor->_bufCSVHeaders();
      _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;

      if (_output_buf_len >= OUTPUT_BUF_SIZE - 50) {
        this->serialConnection->print(_getOutBuf());
        _resetOutBuf();
      }
    }
  }

  if (this->includeChecksum) {
    // Add 'checksum' to the end of row
    strcpy_P(tempBuf, CSV_CHECKSUM);
    memcpy(&(_getOutBuf()[_output_buf_len]), tempBuf, strlen(tempBuf));
    _output_buf_len += strlen(tempBuf);
  } else {
    // remove the last comma added in the loop above since there is no checksum
    _output_buf_len--;
    _getOutBuf()[_output_buf_len] = 0;
  }

  this->serialConnection->println(_getOutBuf());
  _resetOutBuf();
}

/**
 * @brief   Builds a row of values to log in a CSV file
 * @ingroup csvwriter
 * @return  string in the format of "unsigned_long,float,...,integer"
 */
void CSVWriter::serialPrintRow(void) {
  int i = 0;
  int checksum = 0;
  Sensor * sensor;

  // Flush the buffer
  _resetOutBuf();

  // Add timestamp to beginning of row
  // Note: millis is divided by 1000 because it appears that the Experiment
  //       Platform doesn't handle timestamps different from Unix Time in
  //       Seconds very well.
  unsigned long timestamp = this->startTime + (millis() / 1000);
  ultoa(timestamp, _getOutBuf(), 10);
  _output_buf_len = strlen(_getOutBuf());
  _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;

  // Add values(unit),... for each sensor value to the row
  for (i = 0; i < this->numSensors; i++) {
    sensor = this->sensors[i];
    if (sensor->initialized) {
      sensor->read();
      // Because the checksum is calculated by simply summing together all of the values,
      // we can calculate the checksum of each value set independently and then adds those
      // individual checksums together, while `_bufCSVValues()` puts all of the (possibly
      // comma separated) values into the output buffer
      checksum += sensor->_bufCSVValues();
      _getOutBuf()[_output_buf_len++] = CSV_SEPARATOR;

      if (_output_buf_len >= OUTPUT_BUF_SIZE - 50) {
        this->serialConnection->print(_getOutBuf());
        _resetOutBuf();
      }
    }
  }

  if (this->includeChecksum) {
    // Add checksum to the end of row
    itoa(checksum, _getOutBuf() + _output_buf_len, 10);
    _output_buf_len = strlen(_getOutBuf());
  } else {
    // remove the last comma added in the loop above since there is no checksum
    _output_buf_len--;
    _getOutBuf()[_output_buf_len] = 0;
  }

  this->serialConnection->println(_getOutBuf());
  _resetOutBuf();
}
