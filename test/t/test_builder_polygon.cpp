
#include <test.hpp>

#include <vtzero/builder.hpp>
#include <vtzero/geometry.hpp>
#include <vtzero/index.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using polygon_type = std::vector<std::vector<vtzero::point<2>>>;

struct polygon_handler {

    polygon_type data;

    void ring_begin(uint32_t count) {
        data.emplace_back();
        data.back().reserve(count);
    }

    void ring_point(const vtzero::point<2> point) {
        data.back().push_back(point);
    }

    void ring_end(vtzero::ring_type /* type */) const noexcept {
    }

};

void test_polygon_builder(bool with_id, bool with_prop) {
    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};

    {
        vtzero::polygon_feature_builder fbuilder{lbuilder};

        if (with_id) {
            fbuilder.set_id(17);
        }

        fbuilder.add_ring(4);
        fbuilder.set_point(10, 20);
        fbuilder.set_point(vtzero::point<2>{20, 30});
        fbuilder.set_point(mypoint{30, 40});
        fbuilder.set_point(10, 20);

        if (with_prop) {
            fbuilder.add_property("foo", "bar");
        }

        if (with_id) {
            fbuilder.commit();
        }
    }

    const std::string data = tbuilder.serialize();

    vtzero::vector_tile tile{data};

    auto layer = tile.next_layer();
    REQUIRE(layer.name() == "test");
    REQUIRE(layer.version() == 2);
    REQUIRE(layer.extent() == 4096);
    REQUIRE(layer.num_features() == 1);

    const auto feature = layer.next_feature();
    REQUIRE(feature.id() == (with_id ? 17 : 0));

    polygon_handler handler;
    vtzero::decode_polygon_geometry(feature.geometry(), handler);

    const polygon_type result = {{{10, 20}, {20, 30}, {30, 40}, {10, 20}}};
    REQUIRE(handler.data == result);
}

TEST_CASE("polygon builder without id/without properties") {
    test_polygon_builder(false, false);
}

TEST_CASE("polygon builder without id/with properties") {
    test_polygon_builder(false, true);
}

TEST_CASE("polygon builder with id/without properties") {
    test_polygon_builder(true, false);
}

TEST_CASE("polygon builder with id/with properties") {
    test_polygon_builder(true, true);
}

TEST_CASE("Calling add_ring(3) throws assert") {
    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};
    vtzero::polygon_feature_builder fbuilder{lbuilder};

    REQUIRE_THROWS_AS(fbuilder.add_ring(3), assert_error);
}

void test_multipolygon_builder(bool with_id, bool with_prop) {
    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};
    vtzero::polygon_feature_builder fbuilder{lbuilder};

    if (with_id) {
        fbuilder.set_id(17);
    }

    fbuilder.add_ring(4);
    fbuilder.set_point(10, 20);
    fbuilder.set_point(vtzero::point<2>{20, 30});
    fbuilder.set_point(mypoint{30, 40});
    fbuilder.set_point(10, 20);

    fbuilder.add_ring(5);
    fbuilder.set_point(1, 1);
    fbuilder.set_point(2, 1);
    fbuilder.set_point(2, 2);
    fbuilder.set_point(1, 2);

    if (with_id) {
        fbuilder.set_point(1, 1);
    } else {
        fbuilder.close_ring();
    }

    if (with_prop) {
        fbuilder.add_property("foo", vtzero::encoded_property_value{"bar"});
    }

    fbuilder.commit();

    const std::string data = tbuilder.serialize();

    vtzero::vector_tile tile{data};

    auto layer = tile.next_layer();
    REQUIRE(layer.name() == "test");
    REQUIRE(layer.version() == 2);
    REQUIRE(layer.extent() == 4096);
    REQUIRE(layer.num_features() == 1);

    const auto feature = layer.next_feature();
    REQUIRE(feature.id() == (with_id ? 17 : 0));

    polygon_handler handler;
    vtzero::decode_polygon_geometry(feature.geometry(), handler);

    const polygon_type result = {{{10, 20}, {20, 30}, {30, 40}, {10, 20}},
                                 {{1, 1}, {2, 1}, {2, 2}, {1, 2}, {1, 1}}};
    REQUIRE(handler.data == result);
}


TEST_CASE("Multipolygon builder without id/without properties") {
    test_multipolygon_builder(false, false);
}

TEST_CASE("Multipolygon builder without id/with properties") {
    test_multipolygon_builder(false, true);
}

TEST_CASE("Multipolygon builder with id/without properties") {
    test_multipolygon_builder(true, false);
}

TEST_CASE("Multipolygon builder with id/with properties") {
    test_multipolygon_builder(true, true);
}

TEST_CASE("Calling add_ring() twice throws assert") {
    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};
    vtzero::polygon_feature_builder fbuilder{lbuilder};

    fbuilder.add_ring(4);
    REQUIRE_ASSERT(fbuilder.add_ring(4));
}

TEST_CASE("Calling polygon_feature_builder::set_point()/close_ring() throws assert") {
    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};
    vtzero::polygon_feature_builder fbuilder{lbuilder};

    SECTION("set_point") {
        REQUIRE_THROWS_AS(fbuilder.set_point(10, 10), assert_error);
    }
    SECTION("close_ring") {
        REQUIRE_THROWS_AS(fbuilder.close_ring(), assert_error);
    }
}

TEST_CASE("Calling polygon_feature_builder::set_point()/close_ring() too often throws assert") {
    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};
    vtzero::polygon_feature_builder fbuilder{lbuilder};

    fbuilder.add_ring(4);
    fbuilder.set_point(10, 20);
    fbuilder.set_point(20, 20);
    fbuilder.set_point(30, 20);
    fbuilder.set_point(10, 20);

    SECTION("set_point") {
        REQUIRE_THROWS_AS(fbuilder.set_point(50, 20), assert_error);
    }
    SECTION("close_ring") {
        REQUIRE_THROWS_AS(fbuilder.close_ring(), assert_error);
    }
}

TEST_CASE("Add polygon from container") {
    const polygon_type points = {{{10, 20}, {20, 30}, {30, 40}, {10, 20}}};

    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};

    {
        vtzero::polygon_feature_builder fbuilder{lbuilder};

        SECTION("using iterators") {
            fbuilder.add_ring(points[0].cbegin(), points[0].cend());
        }

        SECTION("using iterators and size") {
            fbuilder.add_ring(points[0].cbegin(), points[0].cend(), static_cast<uint32_t>(points[0].size()));
        }

        SECTION("using container directly") {
            fbuilder.add_ring_from_container(points[0]);
        }
    }

    const std::string data = tbuilder.serialize();

    vtzero::vector_tile tile{data};

    auto layer = tile.next_layer();
    REQUIRE(layer);
    REQUIRE(layer.name() == "test");
    REQUIRE(layer.version() == 2);
    REQUIRE(layer.extent() == 4096);
    REQUIRE(layer.num_features() == 1);

    const auto feature = layer.next_feature();

    polygon_handler handler;
    vtzero::decode_polygon_geometry(feature.geometry(), handler);

    REQUIRE(handler.data == points);
}

TEST_CASE("Add polygon from iterator with wrong count throws assert") {
    const std::vector<vtzero::point<2>> points = {{10, 20}, {20, 30}, {30, 40}, {10, 20}};

    vtzero::tile_builder tbuilder;
    vtzero::layer_builder lbuilder{tbuilder, "test"};
    vtzero::polygon_feature_builder fbuilder{lbuilder};

    REQUIRE_THROWS_AS(fbuilder.add_ring(points.cbegin(),
                                        points.cend(),
                                        static_cast<uint32_t>(points.size() + 1)), assert_error);
}

