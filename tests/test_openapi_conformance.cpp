// ─────────────────────────────────────────────────────────────────────────────
// H1.6 — OpenAPI contract test.
//
// Validates live SBI responses against docs/openapi/nwdaf-analytics-v1.yaml so
// the published contract cannot drift from the implementation: change a
// response shape without updating the spec (or vice versa) and CI fails.
//
// The validator covers the JSON Schema subset the spec actually uses — $ref,
// type, required, properties, items, enum, minimum, maximum — implemented on
// yaml-cpp, which is already a dependency. No new third-party library, in
// keeping with the project's dependency-light constraint.
// ─────────────────────────────────────────────────────────────────────────────
#include <catch2/catch_test_macros.hpp>
#include "nwdaf_server.hpp"
#include "nwdaf_analytics.hpp"
#include "nwdaf_subscription.hpp"
#include "mock_open5gs.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <thread>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef NWDAF_OPENAPI_SPEC
#error "NWDAF_OPENAPI_SPEC must point at the OpenAPI document"
#endif

static const int CONF_PORT = 17780;

// ── Minimal JSON Schema validator over yaml-cpp ──────────────────────────────

class SchemaValidator {
public:
    explicit SchemaValidator(YAML::Node root) : root_(std::move(root)) {}

    std::vector<std::string> validate(const json& doc, const YAML::Node& schema,
                                      const std::string& path = "$") const {
        std::vector<std::string> errs;
        check(doc, resolve(schema), path, errs);
        return errs;
    }

    YAML::Node schemaByRef(const std::string& ref) const { return deref(ref); }
    YAML::Node root() const { return root_; }

private:
    YAML::Node root_;

    // "#/components/schemas/Foo" → the node at that JSON pointer.
    //
    // NOTE: yaml-cpp's Node::operator= assigns *content* into the referenced
    // node rather than rebinding the handle, so walking a path with
    // `cur = cur[seg]` silently mutates the loaded document. reset() is the
    // rebinding operation and is what this walk must use.
    YAML::Node deref(const std::string& ref) const {
        YAML::Node cur = root_;
        std::istringstream ss(ref.substr(ref.find('/') + 1));
        std::string seg;
        while (std::getline(ss, seg, '/')) {
            if (seg.empty() || seg == "#") continue;
            const YAML::Node child = cur[seg];
            if (!child) return YAML::Node(YAML::NodeType::Undefined);
            cur.reset(child);
        }
        return cur;
    }

    YAML::Node resolve(const YAML::Node& schema) const {
        if (schema && schema.IsMap() && schema["$ref"])
            return deref(schema["$ref"].as<std::string>());
        return schema;
    }

    static std::string typeName(const json& v) {
        if (v.is_object())  return "object";
        if (v.is_array())   return "array";
        if (v.is_string())  return "string";
        if (v.is_boolean()) return "boolean";
        if (v.is_number_integer() || v.is_number_unsigned()) return "integer";
        if (v.is_number())  return "number";
        if (v.is_null())    return "null";
        return "unknown";
    }

    void check(const json& doc, const YAML::Node& schema,
               const std::string& path, std::vector<std::string>& errs) const {
        if (!schema || !schema.IsMap()) return;

        if (schema["type"]) {
            const std::string want = schema["type"].as<std::string>();
            const std::string got  = typeName(doc);
            bool ok = (want == got)
                   // JSON has one numeric type; an integer is a valid number.
                   || (want == "number" && got == "integer");
            if (!ok) {
                errs.push_back(path + ": expected type " + want + ", got " + got);
                return;  // further checks would be meaningless
            }
        }

        const YAML::Node enum_node = schema["enum"];
        if (enum_node && enum_node.IsSequence() && doc.is_string()) {
            const std::string v = doc.get<std::string>();
            bool found = false;
            for (const auto& e : enum_node)
                if (e.as<std::string>() == v) { found = true; break; }
            if (!found) errs.push_back(path + ": value '" + v + "' is not in the declared enum");
        }

        if (doc.is_number()) {
            const double v = doc.get<double>();
            if (schema["minimum"] && v < schema["minimum"].as<double>())
                errs.push_back(path + ": " + std::to_string(v) + " is below the declared minimum");
            if (schema["maximum"] && v > schema["maximum"].as<double>())
                errs.push_back(path + ": " + std::to_string(v) + " is above the declared maximum");
        }

        if (doc.is_object()) {
            if (schema["required"]) {
                for (const auto& r : schema["required"]) {
                    const std::string key = r.as<std::string>();
                    if (!doc.contains(key))
                        errs.push_back(path + ": missing required property '" + key + "'");
                }
            }
            if (schema["properties"]) {
                const YAML::Node props = schema["properties"];
                for (auto it = props.begin(); it != props.end(); ++it) {
                    const std::string key = it->first.as<std::string>();
                    if (doc.contains(key))
                        check(doc.at(key), resolve(it->second), path + "." + key, errs);
                }
            }
        }

        // oneOf — used where an analytics payload has two legitimate shapes
        // (a populated result, or a short "insufficient data" form). Valid if
        // any branch validates; otherwise report every branch's failures so
        // the diagnostic says why each one was rejected.
        const YAML::Node one_of = schema["oneOf"];
        if (one_of && one_of.IsSequence()) {
            std::vector<std::string> branch_errs;
            bool any_ok = false;
            int branch = 0;
            for (const auto& sub : one_of) {
                std::vector<std::string> sub_errs;
                check(doc, resolve(sub), path, sub_errs);
                if (sub_errs.empty()) { any_ok = true; break; }
                for (const auto& e : sub_errs)
                    branch_errs.push_back("[oneOf branch " + std::to_string(branch) + "] " + e);
                ++branch;
            }
            if (!any_ok)
                errs.insert(errs.end(), branch_errs.begin(), branch_errs.end());
        }

        if (doc.is_array() && schema["items"]) {
            const YAML::Node items = resolve(schema["items"]);
            for (size_t i = 0; i < doc.size(); ++i)
                check(doc[i], items, path + "[" + std::to_string(i) + "]", errs);
        }
    }
};

// ── Fixture ──────────────────────────────────────────────────────────────────

static NwdafConfig confConfig() {
    NwdafConfig cfg;
    cfg.nf_instance_id = "conformance-uuid";
    cfg.plmn_mcc = "999"; cfg.plmn_mnc = "70";
    cfg.served_snssai_sst = 1; cfg.served_snssai_sd = "000001";
    cfg.served_dnn = "internet";
    cfg.sbi_bind_address = "127.0.0.1"; cfg.sbi_port = CONF_PORT;
    cfg.nf_service_names = {{"AMF","amfd"},{"SMF","smfd"},{"UPF","upfd"}};
    cfg.throughput_interfaces = {"ogstun"};
    cfg.throughput_history_size = 360;
    cfg.collection_interval_seconds = 10;
    cfg.amf_journal_lines = 50; cfg.smf_journal_lines = 50;
    cfg.supi_regex = "imsi-(\\d{15})";
    cfg.mongodb_uri = ""; cfg.mongodb_db = "open5gs";
    cfg.nrf_uri = "http://127.0.0.1:7777"; cfg.nrf_register_on_startup = false;
    cfg.model_dir = "/tmp/nwdaf_conformance_models";
    cfg.anomaly_contamination = 0.10; cfg.anomaly_min_samples = 10;
    cfg.baseline_stddev_min_kbps = 0.5; cfg.ewma_alpha = 0.3;
    cfg.rate_limit_per_ip_rps = 0; cfg.rate_limit_global_rps = 0;  // no throttling here
    cfg.log_level = "warn"; cfg.log_file = "/tmp/nwdaf_conformance.log";
    return cfg;
}

struct ConformanceFixture {
    NwdafConfig            cfg;
    MockNwdafCollector     collector;
    NwdafAnalyticsEngine   engine;
    NwdafSubscriptionStore subs;
    NwdafServer            server;
    std::thread            server_thread;
    SchemaValidator        spec;

    ConformanceFixture()
        : cfg(confConfig()),
          collector(cfg),
          engine(collector, cfg),
          server(engine, subs, cfg),
          spec(YAML::LoadFile(NWDAF_OPENAPI_SPEC))
    {
        // Seed a realistic window so analytics return populated payloads
        // rather than only their INSUFFICIENT_DATA branch.
        collector.setNfMetrics({
            {"AMF", "active", 101, 12.0, 51200, 22.0, "LOW"},
            {"SMF", "active", 102, 10.0, 48000, 18.0, "LOW"},
            {"UPF", "active", 103, 30.0, 96000, 41.0, "MEDIUM"},
        });
        collector.setSmfLines({
            "[Established] PDU Session Establishment imsi-999700000000001",
            "[Established] PDU Session Establishment imsi-999700000000002",
            "PDU Session Establishment Reject imsi-999700000000003",
            "[Released] PDU Session Release imsi-999700000000002",
        });
        collector.setAmfLines({
            "Registration complete imsi-999700000000001",
            "Handover required imsi-999700000000002",
            "Authentication failure imsi-999700000000003",
        });
        collector.startBackgroundCollection();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        collector.stopBackgroundCollection();

        for (int i = 0; i < 40; ++i) {
            ThroughputSample s;
            char buf[32];
            snprintf(buf, sizeof(buf), "2026-08-23T11:%02d:00Z", i % 60);
            s.timestamp_iso = buf;
            // A varying series so the anomaly model's variance gate passes.
            s.total_dl_kbps = 400.0 + (i % 7) * 60.0;
            s.total_ul_kbps = 180.0 + (i % 5) * 25.0;
            s.total_dl_bps  = s.total_dl_kbps * 1000.0;
            s.total_ul_bps  = s.total_ul_kbps * 1000.0;
            collector.appendThroughputSample(s);
        }

        server_thread = std::thread([this]{ server.start(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    ~ConformanceFixture() {
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }
};

static std::unique_ptr<ConformanceFixture> g_conf;
static ConformanceFixture& fixture() {
    if (!g_conf) g_conf = std::make_unique<ConformanceFixture>();
    return *g_conf;
}

static void requireValid(const SchemaValidator& v, const json& doc,
                         const YAML::Node& schema, const std::string& what) {
    auto errs = v.validate(doc, schema);
    if (!errs.empty()) {
        std::ostringstream oss;
        oss << what << " violates the OpenAPI contract:";
        for (const auto& e : errs) oss << "\n  - " << e;
        oss << "\nPayload: " << doc.dump(2);
        FAIL(oss.str());
    }
    SUCCEED();
}

// ── Validator self-test ──────────────────────────────────────────────────────
//
// A contract test is only worth its rejections. These cases prove the
// validator actually fails on each violation class it claims to police, so a
// future refactor cannot quietly turn it into a rubber stamp.

TEST_CASE("H1.6: the schema validator rejects each violation class") {
    const YAML::Node schema = YAML::Load(R"(
type: object
required: [id, level, score, items]
properties:
  id:    { type: string }
  level: { type: string, enum: [LOW, HIGH] }
  score: { type: number, minimum: 0, maximum: 100 }
  count: { type: integer }
  items:
    type: array
    items:
      type: object
      required: [name]
      properties:
        name: { type: string }
)");
    SchemaValidator v{YAML::Node(YAML::NodeType::Map)};

    const json good = {{"id", "a"}, {"level", "LOW"}, {"score", 50},
                       {"items", json::array({{{"name", "x"}}})}};
    REQUIRE(v.validate(good, schema).empty());

    SECTION("missing required property") {
        json bad = good; bad.erase("level");
        REQUIRE_FALSE(v.validate(bad, schema).empty());
    }
    SECTION("value outside the declared enum") {
        json bad = good; bad["level"] = "MEDIUM";
        REQUIRE_FALSE(v.validate(bad, schema).empty());
    }
    SECTION("number above the declared maximum") {
        json bad = good; bad["score"] = 101;
        REQUIRE_FALSE(v.validate(bad, schema).empty());
    }
    SECTION("number below the declared minimum") {
        json bad = good; bad["score"] = -1;
        REQUIRE_FALSE(v.validate(bad, schema).empty());
    }
    SECTION("wrong scalar type") {
        json bad = good; bad["id"] = 7;
        REQUIRE_FALSE(v.validate(bad, schema).empty());
    }
    SECTION("a non-integer where an integer is declared") {
        json bad = good; bad["count"] = 1.5;
        REQUIRE_FALSE(v.validate(bad, schema).empty());
    }
    SECTION("violation nested inside an array item") {
        json bad = good; bad["items"] = json::array({{{"nome", "typo"}}});
        REQUIRE_FALSE(v.validate(bad, schema).empty());
    }
    SECTION("an integer satisfies a number-typed field") {
        json ok = good; ok["score"] = 42;   // not 42.0
        REQUIRE(v.validate(ok, schema).empty());
    }
}

TEST_CASE("H1.6: the validator resolves $ref without mutating the document") {
    // yaml-cpp's operator= assigns content rather than rebinding, so a naive
    // pointer walk corrupts the loaded spec. Resolving the same ref twice must
    // yield the same node, and must leave neighbouring nodes untouched.
    const YAML::Node root = YAML::Load(R"(
components:
  schemas:
    First:  { type: object, required: [a] }
    Second: { type: object, required: [b] }
)");
    SchemaValidator v{root};
    for (int i = 0; i < 3; ++i) {
        REQUIRE(v.schemaByRef("#/components/schemas/First")["required"][0].as<std::string>()  == "a");
        REQUIRE(v.schemaByRef("#/components/schemas/Second")["required"][0].as<std::string>() == "b");
    }
    REQUIRE(root["components"]["schemas"].size() == 2);
    REQUIRE_FALSE(v.schemaByRef("#/components/schemas/Missing").IsMap());
}

TEST_CASE("H1.6: oneOf accepts any valid branch and rejects a document matching none") {
    const YAML::Node root = YAML::Load(R"(
components:
  schemas:
    Full:
      type: object
      required: [value]
      properties: { value: { type: number } }
    Declined:
      type: object
      required: [reason]
      properties: { reason: { type: string } }
    Either:
      oneOf:
        - $ref: '#/components/schemas/Full'
        - $ref: '#/components/schemas/Declined'
)");
    SchemaValidator v{root};
    const YAML::Node either = v.schemaByRef("#/components/schemas/Either");

    REQUIRE(v.validate(json{{"value", 1.5}}, either).empty());
    REQUIRE(v.validate(json{{"reason", "INSUFFICIENT_DATA"}}, either).empty());

    auto errs = v.validate(json{{"unrelated", true}}, either);
    REQUIRE_FALSE(errs.empty());
    // The diagnostic must say why every branch was rejected, not just the last.
    REQUIRE(errs.size() >= 2);
}

// ── The spec itself ──────────────────────────────────────────────────────────

TEST_CASE("H1.6: the OpenAPI document is well-formed and complete") {
    auto& f = fixture();
    YAML::Node root = f.spec.root();
    REQUIRE(root["openapi"].as<std::string>().rfind("3.0", 0) == 0);
    REQUIRE(root["paths"].size() > 0);
    REQUIRE(root["components"]["schemas"].size() > 0);

    // Every registered analytics ID must appear in the published enum and in
    // the analData schema mapping — a new ID cannot ship undocumented.
    std::vector<std::string> enum_ids;
    for (const auto& e : root["components"]["schemas"]["AnalyticsId"]["enum"])
        enum_ids.push_back(e.as<std::string>());

    const YAML::Node mapping = root["x-analyticsDataSchemas"];
    for (const auto& id : NwdafAnalyticsEngine::VALID_ANALYTICS_IDS) {
        INFO("analyticsId=" << id);
        REQUIRE(std::find(enum_ids.begin(), enum_ids.end(), id) != enum_ids.end());
        REQUIRE(mapping[id]);
        // ...and the schema it names must actually exist.
        REQUIRE(f.spec.schemaByRef(mapping[id].as<std::string>()).IsMap());
    }
    // No stale entries either: the enum must not advertise IDs the engine
    // does not serve.
    REQUIRE(enum_ids.size() == NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.size());
}

// ── Analytics responses ──────────────────────────────────────────────────────

TEST_CASE("H1.6: GET /analytics conforms for every analytics ID") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    YAML::Node envelope = f.spec.schemaByRef("#/components/schemas/AnalyticsResponse");
    YAML::Node mapping  = f.spec.root()["x-analyticsDataSchemas"];

    for (const auto& id : NwdafAnalyticsEngine::VALID_ANALYTICS_IDS) {
        INFO("analyticsId=" << id);
        auto res = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=" + id);
        REQUIRE(res);
        REQUIRE(res->status == 200);

        json body = json::parse(res->body);
        requireValid(f.spec, body, envelope, "GET /analytics envelope for " + id);
        requireValid(f.spec, body["analData"],
                     f.spec.schemaByRef(mapping[id].as<std::string>()),
                     "analData for " + id);
        // The envelope's id must match what was asked for.
        REQUIRE(body["analyticsId"] == id);
    }
}

TEST_CASE("H1.6: POST /nnwdaf-analyticsinfo conforms for every analytics ID") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    YAML::Node envelope = f.spec.schemaByRef("#/components/schemas/AnalyticsResponse");
    YAML::Node mapping  = f.spec.root()["x-analyticsDataSchemas"];

    for (const auto& id : NwdafAnalyticsEngine::VALID_ANALYTICS_IDS) {
        INFO("analyticsId=" << id);
        json req = {{"analyticsId", id},
                    {"dnn", "internet"},
                    {"snssai", {{"sst", 1}, {"sd", "000001"}}}};
        auto res = cli.Post("/nnwdaf-analyticsinfo/v1/analytics",
                            req.dump(), "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 200);

        json body = json::parse(res->body);
        requireValid(f.spec, body, envelope, "POST analytics envelope for " + id);
        requireValid(f.spec, body["analData"],
                     f.spec.schemaByRef(mapping[id].as<std::string>()),
                     "analData for " + id);
    }
}

// ── Operational endpoints ────────────────────────────────────────────────────

TEST_CASE("H1.6: /health conforms and advertises the live catalogue") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);
    auto res = cli.Get("/nwdaf-analytics/v1/health");
    REQUIRE(res);
    REQUIRE(res->status == 200);

    json body = json::parse(res->body);
    requireValid(f.spec, body,
                 f.spec.schemaByRef("#/components/schemas/HealthResponse"),
                 "GET /health");

    // The advertised list must be exactly what the engine serves.
    auto ids = body["nfProfile"]["nwdafInfo"]["analyticsIds"]
                   .get<std::vector<std::string>>();
    REQUIRE(ids.size() == NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.size());
    for (const auto& id : ids)
        REQUIRE(NwdafAnalyticsEngine::VALID_ANALYTICS_IDS.count(id) == 1);
}

TEST_CASE("H1.6: /ready conforms in both states") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);
    auto res = cli.Get("/nwdaf-analytics/v1/ready");
    REQUIRE(res);
    REQUIRE((res->status == 200 || res->status == 503));
    requireValid(f.spec, json::parse(res->body),
                 f.spec.schemaByRef("#/components/schemas/ReadyResponse"),
                 "GET /ready");
}

TEST_CASE("H1.6: POST /train conforms") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(10);
    auto res = cli.Post("/nwdaf-analytics/v1/train", "", "application/json");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    requireValid(f.spec, json::parse(res->body),
                 f.spec.schemaByRef("#/components/schemas/TrainResponse"),
                 "POST /train");
}

TEST_CASE("H1.6: the subscription lifecycle conforms") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);

    YAML::Node sub_schema = f.spec.schemaByRef("#/components/schemas/Subscription");

    json req = {{"analyticsId", "SM_CONGESTION"},
                {"notifUri", "http://127.0.0.1:9/notify"}};
    auto created = cli.Post("/nwdaf-analytics/v1/subscriptions",
                            req.dump(), "application/json");
    REQUIRE(created);
    REQUIRE(created->status == 201);
    json sub = json::parse(created->body);
    requireValid(f.spec, sub, sub_schema, "POST /subscriptions");

    const std::string sub_id = sub["subId"].get<std::string>();

    auto fetched = cli.Get(("/nwdaf-analytics/v1/subscriptions/" + sub_id).c_str());
    REQUIRE(fetched);
    REQUIRE(fetched->status == 200);
    requireValid(f.spec, json::parse(fetched->body), sub_schema, "GET /subscriptions/{id}");

    auto listed = cli.Get("/nwdaf-analytics/v1/subscriptions");
    REQUIRE(listed);
    REQUIRE(listed->status == 200);
    json arr = json::parse(listed->body);
    REQUIRE(arr.is_array());
    for (const auto& item : arr)
        requireValid(f.spec, item, sub_schema, "GET /subscriptions item");

    auto removed = cli.Delete(("/nwdaf-analytics/v1/subscriptions/" + sub_id).c_str());
    REQUIRE(removed);
    REQUIRE(removed->status == 204);
}

// ── Error responses ──────────────────────────────────────────────────────────

TEST_CASE("H1.6: error responses are RFC 7807 problem documents") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);

    YAML::Node problem = f.spec.schemaByRef("#/components/schemas/ProblemDetails");

    struct Case { const char* url; int status; const char* what; };
    const Case cases[] = {
        {"/nwdaf-analytics/v1/analytics",                          400, "missing analyticsId"},
        {"/nwdaf-analytics/v1/analytics?analyticsId=NOT_AN_ID",    422, "unknown analyticsId"},
        {"/nwdaf-analytics/v1/analytics?analyticsId=NF_LOAD&supi=imsi-1", 400, "supi on NF_LOAD"},
        {"/nwdaf-analytics/v1/subscriptions/does-not-exist",       404, "missing subscription"},
    };
    for (const auto& c : cases) {
        INFO(c.what);
        auto res = cli.Get(c.url);
        REQUIRE(res);
        REQUIRE(res->status == c.status);
        json body = json::parse(res->body);
        requireValid(f.spec, body, problem, std::string("error: ") + c.what);
        REQUIRE(body["status"].get<int>() == c.status);
    }
}

TEST_CASE("H1.6: the uppercase QOS_SUSTAINABILITY alias is accepted") {
    auto& f = fixture();
    httplib::Client cli("127.0.0.1", CONF_PORT);
    cli.set_connection_timeout(5);
    auto res = cli.Get("/nwdaf-analytics/v1/analytics?analyticsId=QOS_SUSTAINABILITY");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    json body = json::parse(res->body);
    // Normalised to the canonical spelling the spec's enum declares.
    REQUIRE(body["analyticsId"] == "QoS_SUSTAINABILITY");
    requireValid(f.spec, body,
                 f.spec.schemaByRef("#/components/schemas/AnalyticsResponse"),
                 "QOS_SUSTAINABILITY alias");
}
