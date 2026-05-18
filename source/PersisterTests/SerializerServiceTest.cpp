#include <gtest/gtest.h>

#include <array>

#include <EngineTestData/DescTestDataFactory.h>

#include <PersisterInterface/SerializerService.h>


class SerializerServiceTests : public ::testing::Test
{
public:
    SerializerServiceTests()
    {
        _descTestDataFactory = &DescTestDataFactory::get();
        _serializerService = &SerializerService::get();
    }

    void testSerializationAndDeserialization(Desc const& data)
    {
        DeserializedSimulation deserializedSimulationBefore{.mainData = data};
        SerializedSimulation serializedSimulation;
        _serializerService->serializeSimulationToStrings(serializedSimulation, deserializedSimulationBefore);

        DeserializedSimulation deserializedSimulationAfter;
        _serializerService->deserializeSimulationFromStrings(deserializedSimulationAfter, serializedSimulation);

        EXPECT_TRUE(_descTestDataFactory->compare(deserializedSimulationBefore.mainData, deserializedSimulationAfter.mainData));
    }

protected:
    DescTestDataFactory* _descTestDataFactory;
    SerializerService* _serializerService;
};

namespace
{
    std::string decodeBase64(std::string const& input)
    {
        std::array<int, 256> reverseLookup;
        reverseLookup.fill(-1);
        for (auto i = 0; i < 26; ++i) {
            reverseLookup['A' + i] = i;
            reverseLookup['a' + i] = i + 26;
        }
        for (auto i = 0; i < 10; ++i) {
            reverseLookup['0' + i] = i + 52;
        }
        reverseLookup[static_cast<unsigned char>('+')] = 62;
        reverseLookup[static_cast<unsigned char>('/')] = 63;

        std::string result;
        int buffer = 0;
        int bits = -8;
        for (auto const ch : input) {
            if (ch == '=') {
                break;
            }
            auto const value = reverseLookup[static_cast<unsigned char>(ch)];
            if (value < 0) {
                continue;
            }
            buffer = (buffer << 6) | value;
            bits += 6;
            if (bits >= 0) {
                result.push_back(static_cast<char>((buffer >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return result;
    }
}

TEST_F(SerializerServiceTests, singleEnergyParticle)
{
    Desc data;
    data._energies.emplace_back(_descTestDataFactory->createNonDefaultEnergyDesc());

    testSerializationAndDeserialization(data);
}

using ObjectParameter = DescTestDataFactory::ObjectParameter;
class SerializerServiceTests_AllCellTypes
    : public SerializerServiceTests
    , public testing::WithParamInterface<ObjectParameter>
{};

INSTANTIATE_TEST_SUITE_P(
    SerializerServiceTests_AllCellTypes,
    SerializerServiceTests_AllCellTypes,
    ::testing::ValuesIn(DescTestDataFactory::get().getAllObjectParameters()));

TEST_P(SerializerServiceTests_AllCellTypes, objectWithEmptyGenome)
{
    auto objectParameter = GetParam();

    Desc data;
    if (objectParameter.objectType == ObjectType_Cell) {
        data.addCreature({_descTestDataFactory->createNonDefaultObjectDesc(objectParameter)}, CreatureDesc(), GenomeDesc());
    } else {
        data.objects({_descTestDataFactory->createNonDefaultObjectDesc(objectParameter)});
    }


    testSerializationAndDeserialization(data);
}

using NodeParameter = DescTestDataFactory::NodeParameter;
class SerializerServiceTests_AllNodeTypes
    : public SerializerServiceTests
    , public testing::WithParamInterface<NodeParameter>
{};

INSTANTIATE_TEST_SUITE_P(
    SerializerServiceTests_AllNodeTypes,
    SerializerServiceTests_AllNodeTypes,
    ::testing::ValuesIn(DescTestDataFactory::get().getAllNodeParameters()));

TEST_P(SerializerServiceTests_AllNodeTypes, objectWithNonEmptyGenome)
{
    auto nodeParameter = GetParam();

    auto [creature, genome] = _descTestDataFactory->createNonDefaultCreatureDesc(nodeParameter);

    auto data = Desc().addCreature({ObjectDesc()}, creature, genome);

    testSerializationAndDeserialization(data);
}

TEST_F(SerializerServiceTests, deserializeLegacyGeneConstructorProperties)
{
    auto constexpr LegacyMainDataBase64 =
        "H4sIAAAAAAAAA+1TuwrCQBC8y8MHgmihWGphoWDQwlr8CuugUQsLCTZa+Vv+nXfJjg7HoSB2OrAkO7s3u+EmuqlKzJNpMp2kh+M+TWZzIVWkntAmAhOhiTpxBd4TjaoQYGJ5BtRlubbNQ2ewXaQi+SHbpesz2scko6TLjspFX6NQpYZy3nUR03tEdYyPRDjgxRUVsdMpzXfZCR01E8PA+V7s4cogD6kvFPmbFHVETT2aaxdXHliRjokWcgdadFq+wx+iW2xavvfd4o8SHXm6l17g4QjN3vN502c9+CXPtuy7y993jB8lXvpuhSJuM6ZOe+PWR8WVux7aeCTbKAADmIc7WR45O24Jnh3H4zUd9AkDXxMagecf6KsH7hcWmR99BwAAH4sIAAAAAAAAAwMAAAAAAAAAAAA=";

    SerializedSimulation serializedSimulation;
    ASSERT_TRUE(_serializerService->serializeSimulationToStrings(serializedSimulation, DeserializedSimulation{}));
    serializedSimulation.mainData = decodeBase64(LegacyMainDataBase64);

    DeserializedSimulation deserializedSimulation;
    ASSERT_TRUE(_serializerService->deserializeSimulationFromStrings(deserializedSimulation, serializedSimulation));

    auto const& constructor = deserializedSimulation.mainData._genomes.at(0)._genes.at(1)._nodes.at(0)._constructor.value();
    EXPECT_TRUE(constructor._separation);
    EXPECT_EQ(4, constructor._numBranches);
    EXPECT_EQ(6, constructor._numConcatenations);

    SerializedSimulation rewrittenSimulation;
    ASSERT_TRUE(_serializerService->serializeSimulationToStrings(rewrittenSimulation, deserializedSimulation));

    DeserializedSimulation reloadedSimulation;
    ASSERT_TRUE(_serializerService->deserializeSimulationFromStrings(reloadedSimulation, rewrittenSimulation));
    EXPECT_TRUE(_descTestDataFactory->compare(deserializedSimulation.mainData, reloadedSimulation.mainData));
}
