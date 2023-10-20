// Header files
#include "../common.h"
#include "mpfr.h"
#include "simdjson.h"
#include "./tradeogre.h"

using namespace std;


// Constants

// Precision
static const mpfr_prec_t PRECISION = 256;


// Supporting function implementation

// Constructor
TradeOgre::TradeOgre(const TorProxy &torProxy) :

	// Delegate constructor
	PriceOracle(torProxy)
{
}

// Get new price
pair<chrono::time_point<chrono::system_clock>, string> TradeOgre::getNewPrice() const {

	// Check if creating MWC request failed
	vector<uint8_t> mwcResponse;
	const unique_ptr mwcRequest = createRequest("tradeogre.com", Common::HTTPS_PORT, "/api/v1/history/MWC-BTC", mwcResponse);
	if(!mwcRequest) {
	
		// Throw exception
		throw runtime_error("Creating TradeOgre MWC request failed");
	}
	
	// Check if creating BTC request failed
	vector<uint8_t> btcResponse;
	const unique_ptr btcRequest = createRequest("tradeogre.com", Common::HTTPS_PORT, "/api/v1/ticker/BTC-USDT", btcResponse);
	if(!btcRequest) {
	
		// Throw exception
		throw runtime_error("Creating TradeOgre BTC request failed");
	}
	
	// Check if performing requests failed
	if(!performRequests() || mwcResponse.empty() || btcResponse.empty()) {
	
		// Throw exception
		throw runtime_error("Performing TradeOgre requests failed");
	}
	
	// Parse MWC response as JSON
	mwcResponse.resize(mwcResponse.size() + simdjson::SIMDJSON_PADDING);
	simdjson::dom::parser parser;
	simdjson::dom::element json = parser.parse(mwcResponse.data(), mwcResponse.size() - simdjson::SIMDJSON_PADDING, false);
	
	// Check if MWC response is invalid
	if(!json.is_array() || !json.get_array().size()) {
	
		// Throw exception
		throw runtime_error("TradeOgre MWC response is invalid");
	}
	
	// Check if MWC most recent price is invalid
	const simdjson::dom::element mostRecentPrice = json.at(json.get_array().size() - 1);
	if(!mostRecentPrice.is_object() || !mostRecentPrice["date"].is_int64() || !mostRecentPrice["price"].is_string()) {
	
		// Throw exception
		throw runtime_error("TradeOgre MWC most recent price is invalid");
	}
	
	// Get date
	const int64_t date = mostRecentPrice["date"].get_int64().value();
	
	// Check if date is invalid
	if(date < chrono::duration_cast<chrono::seconds>(chrono::time_point<chrono::system_clock>::min().time_since_epoch()).count() || date > chrono::duration_cast<chrono::seconds>(chrono::time_point<chrono::system_clock>::max().time_since_epoch()).count()) {
	
		// Throw exception
		throw runtime_error("TradeOgre date is invalid");
	}
	
	// Get timestamp from date
	chrono::time_point<chrono::system_clock> timestamp = chrono::time_point<chrono::system_clock>(chrono::seconds(date));
	
	// Check if timestamp is in the future
	if(timestamp > chrono::system_clock::now()) {
	
		// Set timestamp to now
		timestamp = chrono::system_clock::now();
	}
	
	// Initialize MWC price
	mpfr_t mwcPrice;
	mpfr_init2(mwcPrice, PRECISION);
	
	// Automatically free MWC price
	const unique_ptr<remove_pointer<mpfr_ptr>::type, decltype(&mpfr_clear)> mwcPriceUniquePointer(mwcPrice, mpfr_clear);
	
	// Get price
	const char *price = mostRecentPrice["price"].get_c_str();
	
	// Go through all characters in the price
	for(const char *i = price; *i; ++i) {
	
		// Check if price is invalid
		if(!isdigit(*i) && *i != '.') {
		
			// Throw exception
			throw runtime_error("TradeOgre MWC price is invalid");
		}
	}
	
	// Check if setting MWC price is invalid
	if(mpfr_set_str(mwcPrice, price, Common::DECIMAL_NUMBER_BASE, MPFR_RNDN) || mpfr_sgn(mwcPrice) <= 0) {
	
		// Throw exception
		throw runtime_error("TradeOgre MWC price is invalid");
	}
	
	// Initialize precision
	unsigned int precision = 0;
	
	// Check if price has a decimal
	const char *decimal = strchr(price, '.');
	if(decimal) {
	
		// Update precision
		precision += strlen(price) - (decimal + sizeof('.') - price);
	}
	
	// Parse BTC response as JSON
	btcResponse.resize(btcResponse.size() + simdjson::SIMDJSON_PADDING);
	json = parser.parse(btcResponse.data(), btcResponse.size() - simdjson::SIMDJSON_PADDING, false);
	
	// Check if BTC response is invalid
	if(!json.is_object() || !json["success"].is_bool() || !json["success"].get_bool().value() || !json["price"].is_string()) {
	
		// Throw exception
		throw runtime_error("TradeOgre BTC response is invalid");
	}
	
	// Initialize BTC price
	mpfr_t btcPrice;
	mpfr_init2(btcPrice, PRECISION);
	
	// Automatically free BTC price
	const unique_ptr<remove_pointer<mpfr_ptr>::type, decltype(&mpfr_clear)> btcPriceUniquePointer(btcPrice, mpfr_clear);
	
	// Get price
	price = json["price"].get_c_str();
	
	// Go through all characters in the price
	for(const char *i = price; *i; ++i) {
	
		// Check if price is invalid
		if(!isdigit(*i) && *i != '.') {
		
			// Throw exception
			throw runtime_error("TradeOgre BTC price is invalid");
		}
	}
	
	// Check if setting BTC price is invalid
	if(mpfr_set_str(btcPrice, price, Common::DECIMAL_NUMBER_BASE, MPFR_RNDN) || mpfr_sgn(btcPrice) <= 0) {
	
		// Throw exception
		throw runtime_error("TradeOgre BTC price is invalid");
	}
	
	// Check if price has a decimal
	decimal = strchr(price, '.');
	if(decimal) {
	
		// Update precision
		precision += strlen(price) - (decimal + sizeof('.') - price);
	}
	
	// Multiply MWC price by BTC price to get the price in USDT
	mpfr_mul(mwcPrice, mwcPrice, btcPrice, MPFR_RNDN);
	
	// Check if result is invalid
	if(mpfr_sgn(mwcPrice) <= 0) {
	
		// Throw exception
		throw runtime_error("TradeOgre result is invalid");
	}
	
	// Check if getting result size failed
	const int resultSize = mpfr_snprintf(nullptr, 0, ("%." + to_string(precision) + "R*F").c_str(), MPFR_RNDN, mwcPrice);
	if(resultSize <= 0) {
	
		// Throw exception
		throw runtime_error("Getting TradeOgre result size failed");
	}
	
	// Check if getting result failed
	string result(resultSize, '\0');
	if(mpfr_sprintf(result.data(), ("%." + to_string(precision) + "R*F").c_str(), MPFR_RNDN, mwcPrice) != resultSize) {
	
		// Throw exception
		throw runtime_error("Getting TradeOgre result failed");
	}
	
	// Check if result isn't zero
	if(result != "0") {
	
		// Check if result has a trailing zero
		if(result.back() == '0') {
		
			// Remove trailing zeros from result
			result = result.substr(0, result.find_last_not_of('0') + sizeof('0'));
		}
		
		// Check if result has a trailing decimal
		if(result.back() == '.') {
		
			// Remove trailing decimal from result
			result.pop_back();
		}
	}
	
	// Return time and result
	return {timestamp, result};
}
