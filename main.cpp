// Bike E20 Checker - plain C++17 + SQLite + tiny built-in web server (NO framework)
#include <sqlite3.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>
using namespace std;
using Row = map<string, string>;

static string esc(const string& s) { string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; }

// ---------- Encapsulation: Database hides raw sqlite3 ----------
class Database {
    sqlite3* db_ = nullptr;
public:
    explicit Database(const string& path) { if (sqlite3_open(path.c_str(), &db_)) throw runtime_error("Cannot open DB"); }
    ~Database() { sqlite3_close(db_); }
    vector<Row> query(const string& sql, const vector<string>& args = {}) {
        vector<Row> rows; sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return rows;
        for (size_t i = 0; i < args.size(); i++) sqlite3_bind_text(st, (int)i + 1, args[i].c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(st) == SQLITE_ROW) {
            Row r;
            for (int c = 0; c < sqlite3_column_count(st); c++) { auto t = sqlite3_column_text(st, c); r[sqlite3_column_name(st, c)] = t ? (const char*)t : ""; }
            rows.push_back(r);
        }
        sqlite3_finalize(st); return rows;
    }
    void exec(const string& sql, const vector<string>& args = {}) { query(sql, args); }
};

// ---------- Abstraction + Inheritance + Polymorphism ----------
class Vehicle {
protected: string brand_, model_;
public:
    Vehicle(string b, string m) : brand_(move(b)), model_(move(m)) {}
    virtual ~Vehicle() = default;
    virtual string type() const = 0;
    virtual int serviceIntervalKm() const = 0;
    string name() const { return brand_ + " " + model_; }
};
class Bike : public Vehicle {
    int id_, cc_, yearFrom_; double kmpl_; bool e20_; string note_;   // private = encapsulation
public:
    Bike(const Row& r) : Vehicle(r.at("brand"), r.at("model")), id_(stoi(r.at("id"))), cc_(stoi(r.at("cc"))),
        yearFrom_(stoi(r.at("year_from"))), kmpl_(stod(r.at("kmpl"))), e20_(r.at("e20") == "1"), note_(r.at("note")) {}
    double expectedKmpl() const { return kmpl_; }
    string toJson() const {
        ostringstream o;
        o << "{\"id\":" << id_ << ",\"name\":\"" << esc(name()) << "\",\"brand\":\"" << esc(brand_) << "\",\"model\":\"" << esc(model_)
          << "\",\"cc\":" << cc_ << ",\"category\":\"" << type() << "\",\"e20\":" << (e20_ ? "true" : "false")
          << ",\"year_from\":" << yearFrom_ << ",\"kmpl\":" << kmpl_ << ",\"service_interval\":" << serviceIntervalKm()
          << ",\"note\":\"" << esc(note_) << "\",\"source\":\"" << (e20_ ? "Manufacturer / Regulation" : "Unverified") << "\"}";
        return o.str();
    }
};
class Commuter : public Bike { public: using Bike::Bike; string type() const override { return "Commuter"; } int serviceIntervalKm() const override { return 3000; } };
class Sports   : public Bike { public: using Bike::Bike; string type() const override { return "Sports"; }   int serviceIntervalKm() const override { return 4000; } };
class Cruiser  : public Bike { public: using Bike::Bike; string type() const override { return "Cruiser"; }  int serviceIntervalKm() const override { return 5000; } };
class Scooter  : public Bike { public: using Bike::Bike; string type() const override { return "Scooter"; }  int serviceIntervalKm() const override { return 3000; } };

unique_ptr<Bike> makeBike(const Row& r) {            // factory -> base-class pointer
    const string& c = r.at("category");
    if (c == "Sports") return make_unique<Sports>(r);
    if (c == "Cruiser") return make_unique<Cruiser>(r);
    if (c == "Scooter") return make_unique<Scooter>(r);
    return make_unique<Commuter>(r);
}

// ---------- Service classes ----------
class MileageCalculator {
public:
    static double kmpl(double km, double l) { return l > 0 ? km / l : 0; }
    static double costPerKm(double km, double l, double price) { return km > 0 ? l * price / km : 0; }
};
class MaintenanceReminder {
public:
    static string check(const Bike& b, double last, double cur) {
        double interval = b.serviceIntervalKm(), used = cur - last, left = interval - used;
        string st = left < 0 ? "Overdue" : (left <= 500 ? "Due Soon" : "OK");
        ostringstream o;
        o << "{\"interval\":" << interval << ",\"used\":" << used << ",\"remaining\":" << left << ",\"status\":\"" << st << "\",\"next_service_at\":" << last + interval << "}";
        return o.str();
    }
};

// ---------- Helpers ----------
Database* DB;
unique_ptr<Bike> loadBike(const string& id) { auto r = DB->query("SELECT * FROM bikes WHERE id=?", {id}); return r.empty() ? nullptr : makeBike(r[0]); }
string bikesJson(const vector<Row>& rows) { string s = "["; for (size_t i = 0; i < rows.size(); i++) s += (i ? "," : "") + makeBike(rows[i])->toJson(); return s + "]"; }
double num(const string& body, const string& key) {          // read a number from a flat JSON body
    auto p = body.find("\"" + key + "\""); if (p == string::npos) return 0;
    p = body.find(':', p); return p == string::npos ? 0 : atof(body.c_str() + p + 1);
}
string urlDecode(const string& s) {
    string o; for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) { o += (char)stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; }
        else o += s[i] == '+' ? ' ' : s[i];
    } return o;
}
string param(const string& query, const string& key) {
    size_t p = 0; while (p < query.size()) {
        size_t e = query.find('&', p); if (e == string::npos) e = query.size();
        string kv = query.substr(p, e - p); auto eq = kv.find('=');
        if (kv.substr(0, eq) == key && eq != string::npos) return urlDecode(kv.substr(eq + 1));
        p = e + 1;
    } return "";
}
void initDb() {
    DB->exec("CREATE TABLE IF NOT EXISTS bikes(id INTEGER PRIMARY KEY, brand TEXT, model TEXT, cc INT, category TEXT, e20 INT, year_from INT, kmpl REAL, note TEXT)");
    DB->exec("CREATE TABLE IF NOT EXISTS mileage_logs(id INTEGER PRIMARY KEY, bike_id INT, distance REAL, fuel REAL, kmpl REAL, logged_at TEXT DEFAULT (datetime('now','localtime')))");
    DB->exec("CREATE TABLE IF NOT EXISTS service_logs(id INTEGER PRIMARY KEY, bike_id INT, last_service_km REAL, current_km REAL, logged_at TEXT DEFAULT (datetime('now','localtime')))");
    if (DB->query("SELECT id FROM bikes LIMIT 1").empty()) {
        ifstream f("data/bikes.csv"); string line; getline(f, line);
        while (getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            vector<string> p; stringstream ss(line); string x;
            while (getline(ss, x, ',')) p.push_back(x);
            if (p.size() == 8) DB->exec("INSERT INTO bikes(brand,model,cc,category,e20,year_from,kmpl,note) VALUES(?,?,?,?,?,?,?,?)", p);
        }
    }
}

// ---------- API routing ----------
string api(const string& method, const string& path, const string& query, const string& body, int& status) {
    status = 200;
    if (path == "/api/stats") {
        ostringstream o; o << "{\"brands\":" << DB->query("SELECT COUNT(DISTINCT brand) n FROM bikes")[0]["n"]
            << ",\"models\":" << DB->query("SELECT COUNT(*) n FROM bikes")[0]["n"]
            << ",\"compatible_pct\":" << DB->query("SELECT CAST(ROUND(100.0*SUM(e20)/COUNT(*)) AS INT) n FROM bikes")[0]["n"] << "}";
        return o.str();
    }
    if (path == "/api/brands") {
        string s = "["; auto r = DB->query("SELECT DISTINCT brand FROM bikes ORDER BY brand");
        for (size_t i = 0; i < r.size(); i++) s += (i ? ",\"" : "\"") + esc(r[i]["brand"]) + "\"";
        return s + "]";
    }
    if (path == "/api/bikes") {
        string b = param(query, "brand");
        return bikesJson(b.empty() ? DB->query("SELECT * FROM bikes ORDER BY brand, model") : DB->query("SELECT * FROM bikes WHERE brand=? ORDER BY model", {b}));
    }
    if (path == "/api/search") { string q = "%" + param(query, "q") + "%"; return bikesJson(DB->query("SELECT * FROM bikes WHERE model LIKE ? OR brand LIKE ? LIMIT 10", {q, q})); }
    if (path.rfind("/api/bike/", 0) == 0) { auto b = loadBike(path.substr(10)); if (b) return b->toJson(); status = 404; return "{\"error\":\"not found\"}"; }
    if (path == "/api/mileage" && method == "POST") {
        double km = num(body, "distance"), l = num(body, "fuel"), price = num(body, "price"), k = MileageCalculator::kmpl(km, l);
        string id = to_string((int)num(body, "bike_id"));
        DB->exec("INSERT INTO mileage_logs(bike_id,distance,fuel,kmpl) VALUES(?,?,?,?)", {id, to_string(km), to_string(l), to_string(k)});
        auto b = loadBike(id); ostringstream o;
        o << "{\"kmpl\":" << k << ",\"cost_per_km\":" << MileageCalculator::costPerKm(km, l, price) << ",\"expected\":" << (b ? b->expectedKmpl() : 0) << "}";
        return o.str();
    }
    if (path.rfind("/api/mileage/", 0) == 0) {
        auto r = DB->query("SELECT kmpl, logged_at FROM mileage_logs WHERE bike_id=? ORDER BY id", {path.substr(13)});
        string s = "["; for (size_t i = 0; i < r.size(); i++) s += (i ? "," : "") + string("{\"kmpl\":") + r[i]["kmpl"] + ",\"date\":\"" + r[i]["logged_at"] + "\"}";
        return s + "]";
    }
    if (path == "/api/maintenance" && method == "POST") {
        string id = to_string((int)num(body, "bike_id")); auto b = loadBike(id);
        if (!b) { status = 404; return "{\"error\":\"not found\"}"; }
        double last = num(body, "last_service_km"), cur = num(body, "current_km");
        DB->exec("INSERT INTO service_logs(bike_id,last_service_km,current_km) VALUES(?,?,?)", {id, to_string(last), to_string(cur)});
        return MaintenanceReminder::check(*b, last, cur);
    }
    status = 404; return "{\"error\":\"unknown route\"}";
}

// ---------- Tiny HTTP server ----------
string mime(const string& p) {
    auto e = p.substr(p.rfind('.') + 1);
    return e == "html" ? "text/html" : e == "css" ? "text/css" : e == "js" ? "application/javascript" : "text/plain";
}
string readRequest(int fd) {
    string d; char buf[4096]; size_t he;
    while ((he = d.find("\r\n\r\n")) == string::npos) { ssize_t n = recv(fd, buf, sizeof buf, 0); if (n <= 0) return d; d.append(buf, n); }
    string low = d; transform(low.begin(), low.end(), low.begin(), ::tolower);
    size_t cl = 0, p = low.find("content-length:"); if (p != string::npos) cl = stoul(low.substr(p + 15));
    while (d.size() < he + 4 + cl) { ssize_t n = recv(fd, buf, sizeof buf, 0); if (n <= 0) break; d.append(buf, n); }
    return d;
}
void handleClient(int fd) {
    string req = readRequest(fd); if (req.empty()) return;
    istringstream is(req); string method, target; is >> method >> target;
    string body = req.substr(min(req.size(), req.find("\r\n\r\n") + 4));
    string path = target, query; auto q = target.find('?'); if (q != string::npos) { path = target.substr(0, q); query = target.substr(q + 1); }
    int status = 200; string out, type = "application/json";
    if (path.rfind("/api/", 0) == 0) out = api(method, path, query, body, status);
    else {
        string file = (path == "/") ? "static/index.html" : path.substr(1);
        ifstream f(file, ios::binary);
        if (file.find("..") != string::npos || file.rfind("static/", 0) != 0 || !f) { status = 404; out = "Not found"; type = "text/plain"; }
        else { stringstream ss; ss << f.rdbuf(); out = ss.str(); type = mime(file); }
    }
    string head = "HTTP/1.1 " + to_string(status) + (status == 200 ? " OK" : " Not Found") + "\r\nContent-Type: " + type +
                  "; charset=utf-8\r\nContent-Length: " + to_string(out.size()) + "\r\nConnection: close\r\n\r\n";
    string resp = head + out; send(fd, resp.data(), resp.size(), 0);
}

int main() {
    Database db("bikes.db"); DB = &db; initDb();
    int srv = socket(AF_INET, SOCK_STREAM, 0), yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(8080);
    if (bind(srv, (sockaddr*)&addr, sizeof addr) < 0 || listen(srv, 10) < 0) { cerr << "Port 8080 busy\n"; return 1; }
    cout << "Bike E20 Checker running -> open http://localhost:8080\n";
    while (true) { int c = accept(srv, nullptr, nullptr); if (c < 0) continue; handleClient(c); close(c); }
}
