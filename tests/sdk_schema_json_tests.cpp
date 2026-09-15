// The wire JSON of a config schema, form values and field choices: the exact
// byte spelling core parses. Nothing here is generic JSON round-tripping -
// key order and omission rules are the contract, so every case compares the
// literal string a caller would see on the socket.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/sdk/schema_json.h"

#include <string>

using namespace phicore::adapter::v1;
using namespace phicore::adapter::sdk;

namespace {

const std::string kDefaultFieldLayoutJson =
    "\"layout\":{\"position\":0,\"cells\":1,\"newRow\":false,\"controlWidth\":\"Normal\","
    "\"labelPosition\":\"Auto\",\"actionPosition\":\"Auto\"}";

const std::string kDefaultSectionJson =
    "{\"title\":\"\",\"description\":\"\",\"layout\":{\"width\":\"Normal\",\"columns\":1,"
    "\"labelWidth\":\"Normal\"},\"fields\":[]}";

std::string schemaWithFactoryField(const AdapterConfigField &field)
{
    AdapterConfigSchema schema;
    schema.factory.fields.push_back(field);
    return configSchemaToJson(schema);
}

// Wraps one field's expected JSON into the full two-section schema document,
// with an otherwise-default factory section and a fully default instance.
std::string wrapFactoryField(const std::string &fieldJson)
{
    return "{\"factory\":{\"title\":\"\",\"description\":\"\",\"layout\":{\"width\":\"Normal\","
           "\"columns\":1,\"labelWidth\":\"Normal\"},\"fields\":[" +
           fieldJson + "]},\"instance\":" + kDefaultSectionJson + "}";
}

void testDefaultConstructedSchemaIsTwoEmptySections()
{
    AdapterConfigSchema schema;
    const std::string expected = "{\"factory\":" + kDefaultSectionJson + ",\"instance\":" + kDefaultSectionJson + "}";
    PHI_CHECK(configSchemaToJson(schema) == expected);
}

void testDefaultConstructedFieldOmitsEveryOptionalMember()
{
    AdapterConfigField field; // key="", type=String, label=""
    const std::string expectedField = "{\"key\":\"\",\"type\":\"String\",\"label\":\"\"," + kDefaultFieldLayoutJson + "}";
    PHI_CHECK(schemaWithFactoryField(field) == wrapFactoryField(expectedField));
}

void testFieldWithEveryOptionalMemberSet()
{
    AdapterConfigField field;
    field.key = "k";
    field.type = AdapterConfigFieldType::Select;
    field.label = "L";
    field.description = "D";
    field.placeholder = "P";
    field.defaultValue = ScalarValue(std::int64_t{42});
    field.flags = AdapterConfigFieldFlag::Required | AdapterConfigFieldFlag::Secret;
    field.options = {{"a", "A"}, {"b", ""}}; // "b" has no label: falls back to its value
    field.choicesFrom = "cf";
    field.perChoiceOf = "pc";
    field.parentActionId = "pa";
    field.actions = {{"a1", "A1"}};
    field.visibility.fieldKey = "other";
    field.visibility.value = ScalarValue(true);
    field.visibility.op = AdapterConfigVisibilityOp::Contains;
    field.layout.position = 3;
    field.layout.cells = 2;
    field.layout.newRow = true;
    field.layout.controlWidth = AdapterConfigSize::Wide;
    field.layout.labelPosition = AdapterConfigLabelPosition::Top;
    field.layout.actionPosition = AdapterConfigActionPosition::Below;
    field.minValue = 1.5;
    field.maxValue = 99.5;
    field.step = 0.5;
    field.appendTo = "history";
    field.reloadsForm = true;

    const std::string expectedField =
        "{\"key\":\"k\",\"type\":\"Select\",\"label\":\"L\",\"description\":\"D\",\"placeholder\":\"P\","
        "\"default\":42,\"flags\":[\"Required\",\"Secret\"],"
        "\"choices\":[{\"value\":\"a\",\"label\":\"A\"},{\"value\":\"b\",\"label\":\"b\"}],"
        "\"choicesFrom\":\"cf\",\"perChoiceOf\":\"pc\",\"parentActionId\":\"pa\","
        "\"actions\":[{\"id\":\"a1\",\"label\":\"A1\"}],"
        "\"visibility\":{\"fieldKey\":\"other\",\"value\":true,\"op\":\"Contains\"},"
        "\"layout\":{\"position\":3,\"cells\":2,\"newRow\":true,\"controlWidth\":\"Wide\","
        "\"labelPosition\":\"Top\",\"actionPosition\":\"Below\"},"
        "\"min\":1.5,\"max\":99.5,\"step\":0.5,\"appendTo\":\"history\",\"reloadsForm\":true}";
    PHI_CHECK(schemaWithFactoryField(field) == wrapFactoryField(expectedField));
}

void testMinMaxStepAppendToReloadsFormOmittedThenPresent()
{
    AdapterConfigField field;
    field.key = "k";
    field.label = "L";
    // All absent: nothing beyond layout.
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson + "}"));

    // min/max/step set independently of one another.
    field.minValue = 0.0;
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson +
                                ",\"min\":0}"));
    field.minValue.reset();

    field.maxValue = 10.0;
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson +
                                ",\"max\":10}"));
    field.maxValue.reset();

    field.step = 2.0;
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson +
                                ",\"step\":2}"));
    field.step.reset();

    // appendTo empty: omitted; set: present.
    PHI_CHECK(field.appendTo.empty());
    field.appendTo = "targets";
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson +
                                ",\"appendTo\":\"targets\"}"));
    field.appendTo.clear();

    // reloadsForm false: omitted; true: present.
    PHI_CHECK(!field.reloadsForm);
    field.reloadsForm = true;
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson +
                                ",\"reloadsForm\":true}"));
}

void testEveryFieldTypeNameSerializesToItsWireName()
{
    struct TypeCase {
        AdapterConfigFieldType type;
        const char *name;
    };
    static const TypeCase kCases[] = {
        {AdapterConfigFieldType::String, "String"},
        {AdapterConfigFieldType::Password, "Password"},
        {AdapterConfigFieldType::Integer, "Integer"},
        {AdapterConfigFieldType::Boolean, "Boolean"},
        {AdapterConfigFieldType::Hostname, "Hostname"},
        {AdapterConfigFieldType::Port, "Port"},
        {AdapterConfigFieldType::QrCode, "QrCode"},
        {AdapterConfigFieldType::Select, "Select"},
        {AdapterConfigFieldType::Actions, "Actions"},
        {AdapterConfigFieldType::Section, "Section"},
    };
    for (const TypeCase &c : kCases) {
        AdapterConfigField field;
        field.key = "k";
        field.type = c.type;
        field.label = "L";
        const std::string expectedField =
            "{\"key\":\"k\",\"type\":\"" + std::string(c.name) + "\",\"label\":\"L\"," + kDefaultFieldLayoutJson + "}";
        PHI_CHECK_MSG(schemaWithFactoryField(field) == wrapFactoryField(expectedField), "type=%s", c.name);
    }
}

void testFormLayoutCoversEverySizeAndColumnCount()
{
    struct SizeCase {
        AdapterConfigSize size;
        const char *name;
    };
    static const SizeCase kSizes[] = {
        {AdapterConfigSize::Normal, "Normal"},
        {AdapterConfigSize::Narrow, "Narrow"},
        {AdapterConfigSize::Wide, "Wide"},
    };
    for (const SizeCase &w : kSizes) {
        for (const SizeCase &lw : kSizes) {
            AdapterConfigSchema schema;
            schema.factory.layout.width = w.size;
            schema.factory.layout.columns = 3;
            schema.factory.layout.labelWidth = lw.size;
            const std::string expectedFactory =
                "{\"title\":\"\",\"description\":\"\",\"layout\":{\"width\":\"" + std::string(w.name) +
                "\",\"columns\":3,\"labelWidth\":\"" + std::string(lw.name) + "\"},\"fields\":[]}";
            const std::string expected =
                "{\"factory\":" + expectedFactory + ",\"instance\":" + kDefaultSectionJson + "}";
            PHI_CHECK_MSG(configSchemaToJson(schema) == expected, "width=%s labelWidth=%s", w.name, lw.name);
        }
    }
}

void testFieldLayoutCoversEveryControlWidthLabelAndActionPosition()
{
    struct SizeCase {
        AdapterConfigSize size;
        const char *name;
    };
    struct LabelCase {
        AdapterConfigLabelPosition pos;
        const char *name;
    };
    struct ActionCase {
        AdapterConfigActionPosition pos;
        const char *name;
    };
    static const SizeCase kSizes[] = {
        {AdapterConfigSize::Normal, "Normal"},
        {AdapterConfigSize::Narrow, "Narrow"},
        {AdapterConfigSize::Wide, "Wide"},
    };
    static const LabelCase kLabelPositions[] = {
        {AdapterConfigLabelPosition::Auto, "Auto"},
        {AdapterConfigLabelPosition::Top, "Top"},
        {AdapterConfigLabelPosition::None, "None"},
    };
    static const ActionCase kActionPositions[] = {
        {AdapterConfigActionPosition::Auto, "Auto"},
        {AdapterConfigActionPosition::Below, "Below"},
    };

    for (const SizeCase &cw : kSizes) {
        for (const LabelCase &lp : kLabelPositions) {
            for (const ActionCase &ap : kActionPositions) {
                AdapterConfigField field;
                field.key = "k";
                field.label = "L";
                field.layout.controlWidth = cw.size;
                field.layout.labelPosition = lp.pos;
                field.layout.actionPosition = ap.pos;
                const std::string expectedField =
                    "{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\",\"layout\":{\"position\":0,"
                    "\"cells\":1,\"newRow\":false,\"controlWidth\":\"" +
                    std::string(cw.name) + "\",\"labelPosition\":\"" + std::string(lp.name) +
                    "\",\"actionPosition\":\"" + std::string(ap.name) + "\"}}";
                PHI_CHECK_MSG(schemaWithFactoryField(field) == wrapFactoryField(expectedField),
                              "controlWidth=%s labelPosition=%s actionPosition=%s", cw.name, lp.name, ap.name);
            }
        }
    }
}

void testFlagsOmittedForNoneAndListedInDeclarationOrderOtherwise()
{
    AdapterConfigField none;
    none.key = "k";
    none.label = "L";
    PHI_CHECK(schemaWithFactoryField(none) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson + "}"));

    AdapterConfigField single;
    single.key = "k";
    single.label = "L";
    single.flags = AdapterConfigFieldFlag::ReadOnly;
    PHI_CHECK(schemaWithFactoryField(single) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\",\"flags\":[\"ReadOnly\"]," +
                                kDefaultFieldLayoutJson + "}"));

    AdapterConfigField all;
    all.key = "k";
    all.label = "L";
    all.flags = AdapterConfigFieldFlag::Required | AdapterConfigFieldFlag::Secret |
                AdapterConfigFieldFlag::ReadOnly | AdapterConfigFieldFlag::Transient |
                AdapterConfigFieldFlag::Multi | AdapterConfigFieldFlag::InstanceOnly;
    PHI_CHECK(schemaWithFactoryField(all) ==
              wrapFactoryField(
                  "{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\","
                  "\"flags\":[\"Required\",\"Secret\",\"ReadOnly\",\"Transient\",\"Multi\",\"InstanceOnly\"]," +
                  kDefaultFieldLayoutJson + "}"));
}

void testDefaultValueSerializesEachScalarKind()
{
    AdapterConfigField field;
    field.key = "k";
    field.label = "L";

    field.defaultValue = ScalarValue(true);
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\",\"default\":true," +
                                kDefaultFieldLayoutJson + "}"));

    field.defaultValue = ScalarValue(false);
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\",\"default\":false," +
                                kDefaultFieldLayoutJson + "}"));

    field.defaultValue = ScalarValue(std::int64_t{-7});
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\",\"default\":-7," +
                                kDefaultFieldLayoutJson + "}"));

    field.defaultValue = ScalarValue(3.5);
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\",\"default\":3.5," +
                                kDefaultFieldLayoutJson + "}"));

    field.defaultValue = ScalarValue(Utf8String("hello"));
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\",\"default\":\"hello\"," +
                                kDefaultFieldLayoutJson + "}"));

    // Reset to monostate: omitted again, same as the default-constructed case.
    field.defaultValue = ScalarValue();
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson + "}"));
}

void testVisibilityOmittedWhenFieldKeyEmptyPresentOtherwise()
{
    AdapterConfigField field;
    field.key = "k";
    field.label = "L";
    // fieldKey empty: omitted even though value/op are non-default.
    field.visibility.op = AdapterConfigVisibilityOp::Contains;
    field.visibility.value = ScalarValue(std::int64_t{5});
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\"," + kDefaultFieldLayoutJson + "}"));

    field.visibility.fieldKey = "other";
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\","
                                "\"visibility\":{\"fieldKey\":\"other\",\"value\":5,\"op\":\"Contains\"}," +
                                kDefaultFieldLayoutJson + "}"));

    field.visibility.op = AdapterConfigVisibilityOp::Equals;
    field.visibility.value = ScalarValue(Utf8String("on"));
    PHI_CHECK(schemaWithFactoryField(field) ==
              wrapFactoryField("{\"key\":\"k\",\"type\":\"String\",\"label\":\"L\","
                                "\"visibility\":{\"fieldKey\":\"other\",\"value\":\"on\",\"op\":\"Equals\"}," +
                                kDefaultFieldLayoutJson + "}"));
}

void testStringsAreJsonEscaped()
{
    AdapterConfigField field;
    field.key = "k";
    field.label = "a\"b\\c\nd"; // quote, backslash, newline
    const std::string expectedLabelJson = "\"a\\\"b\\\\c\\nd\"";
    const std::string expectedField =
        "{\"key\":\"k\",\"type\":\"String\",\"label\":" + expectedLabelJson + "," + kDefaultFieldLayoutJson + "}";
    PHI_CHECK(schemaWithFactoryField(field) == wrapFactoryField(expectedField));
}

void testFormValuesAllThreeVariantsAndMixed()
{
    AdapterFormValues values;
    values.push_back(AdapterFormValue{"scalarStr", ScalarValue(Utf8String("hi"))});
    values.push_back(AdapterFormValue{"", ScalarValue(Utf8String("skip me: empty key"))});
    values.push_back(AdapterFormValue{
        "list", ScalarList{ScalarValue(std::int64_t{1}), ScalarValue(Utf8String("two")), ScalarValue(true)}});
    const AdapterConfigPerChoiceValues perChoice = {
        {"choiceA", ScalarValue(std::int64_t{5})},
        {"choiceB", ScalarValue(Utf8String("x"))},
    };
    values.push_back(AdapterFormValue{"perchoice", perChoice});

    const std::string expected =
        "{\"scalarStr\":\"hi\",\"list\":[1,\"two\",true],\"perchoice\":{\"choiceA\":5,\"choiceB\":\"x\"}}";
    PHI_CHECK(formValuesToJson(values) == expected);
}

void testFormValuesEmptyListProducesEmptyObject()
{
    PHI_CHECK(formValuesToJson({}) == "{}");
}

void testFieldChoicesLabelFallsBackToValueAndEmptyKeySkipped()
{
    AdapterFieldChoicesList choices;
    choices.push_back(AdapterFieldChoices{"colors", {{"red", "Red"}, {"blue", ""}}});
    choices.push_back(AdapterFieldChoices{"", {{"x", "X"}}}); // empty key: skipped

    const std::string expected = "{\"colors\":[{\"value\":\"red\",\"label\":\"Red\"},{\"value\":\"blue\",\"label\":\"blue\"}]}";
    PHI_CHECK(fieldChoicesToJson(choices) == expected);
}

void testFieldChoicesEmptyListProducesEmptyObject()
{
    PHI_CHECK(fieldChoicesToJson({}) == "{}");
}

// formValuesFromJson: scalars, arrays of scalars, objects of scalars; anything
// nested deeper is skipped rather than parsed. Round-tripped back through
// formValuesToJson to assert on the parsed shape without touching the variant
// internals directly.

void testFormValuesFromJsonParsesScalars()
{
    const auto values = formValuesFromJson("{\"a\":1,\"b\":\"x\",\"c\":true}");
    PHI_CHECK(formValuesToJson(values) == "{\"a\":1,\"b\":\"x\",\"c\":true}");
}

void testFormValuesFromJsonParsesListsOfScalars()
{
    const auto values = formValuesFromJson("{\"list\":[1,\"two\",true]}");
    PHI_CHECK(formValuesToJson(values) == "{\"list\":[1,\"two\",true]}");
}

void testFormValuesFromJsonParsesObjectsOfScalarsAsPerChoiceValues()
{
    const auto values = formValuesFromJson("{\"perchoice\":{\"x\":1,\"y\":\"z\"}}");
    PHI_CHECK(formValuesToJson(values) == "{\"perchoice\":{\"x\":1,\"y\":\"z\"}}");
}

void testFormValuesFromJsonNonObjectTextYieldsNothing()
{
    PHI_CHECK(formValuesToJson(formValuesFromJson("\"just a string\"")) == "{}");
    PHI_CHECK(formValuesToJson(formValuesFromJson("[1,2,3]")) == "{}");
    PHI_CHECK(formValuesToJson(formValuesFromJson("")) == "{}");
}

void testFormValuesFromJsonSkipsElementsNestedDeeperThanAScalar()
{
    // A non-scalar element inside a list is dropped, the rest of the list kept.
    const auto listValues = formValuesFromJson("{\"list\":[1,{\"x\":1},3]}");
    PHI_CHECK(formValuesToJson(listValues) == "{\"list\":[1,3]}");

    // A non-scalar entry inside a per-choice object is dropped the same way.
    const auto perChoiceValues = formValuesFromJson("{\"perchoice\":{\"a\":1,\"b\":{\"c\":2}}}");
    PHI_CHECK(formValuesToJson(perChoiceValues) == "{\"perchoice\":{\"a\":1}}");

    // An object two levels deep: every entry of the inner object is dropped,
    // leaving the key with an empty per-choice map rather than being skipped.
    const auto deepValues = formValuesFromJson("{\"deep\":{\"inner\":{\"x\":1}}}");
    PHI_CHECK(formValuesToJson(deepValues) == "{\"deep\":{}}");
}

} // namespace

int main()
{
    testDefaultConstructedSchemaIsTwoEmptySections();
    testDefaultConstructedFieldOmitsEveryOptionalMember();
    testFieldWithEveryOptionalMemberSet();
    testMinMaxStepAppendToReloadsFormOmittedThenPresent();
    testEveryFieldTypeNameSerializesToItsWireName();
    testFormLayoutCoversEverySizeAndColumnCount();
    testFieldLayoutCoversEveryControlWidthLabelAndActionPosition();
    testFlagsOmittedForNoneAndListedInDeclarationOrderOtherwise();
    testDefaultValueSerializesEachScalarKind();
    testVisibilityOmittedWhenFieldKeyEmptyPresentOtherwise();
    testStringsAreJsonEscaped();
    testFormValuesAllThreeVariantsAndMixed();
    testFormValuesEmptyListProducesEmptyObject();
    testFieldChoicesLabelFallsBackToValueAndEmptyKeySkipped();
    testFieldChoicesEmptyListProducesEmptyObject();
    testFormValuesFromJsonParsesScalars();
    testFormValuesFromJsonParsesListsOfScalars();
    testFormValuesFromJsonParsesObjectsOfScalarsAsPerChoiceValues();
    testFormValuesFromJsonNonObjectTextYieldsNothing();
    testFormValuesFromJsonSkipsElementsNestedDeeperThanAScalar();
    return phi::testing::report("sdk_schema_json_tests");
}
