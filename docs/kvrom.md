<!--
SPDX-FileCopyrightText: 2026 SombrAbsol

SPDX-License-Identifier: MIT
-->

# KVROM File Format
Used to store strings in *Pokémon Trading Card Game Pocket*.

## Table of contents
* [Format specifications](#format-specifications)
  * [Overview](#overview)
  * [File header](#file-header)
  * [Hash table](#hash-table)
  * [Entry structure](#entry-structure)
  * [Entry payload](#entry-payload)
  * [Key types](#key-types)
  * [Duplicate keys](#duplicate-keys)
* [Control tags in string values](#control-tags-in-string-values)
  * [Spacing tags](#spacing-tags)
  * [Text style tags](#text-style-tags)
  * [Inline image tags](#inline-image-tags)
  * [Numeric insertion tags](#numeric-insertion-tags)
  * [Grammatical agreement tags](#grammatical-agreement-tags)
  * [Date and time insertion tags](#date-and-time-insertion-tags)
  * [String insertion tags](#string-insertion-tags)
  * [Dynamic text insertion tags](#dynamic-text-insertion-tags)
  * [Master-data reference tags](#master-data-reference-tags)

## Format specifications
### Overview
* Integer values are little-endian
* String values encoded as UTF-16-LE
* Keys stored as UTF-16-LE strings (types 0 and 1) or 64-bit little-endian integers (type 2)
* Entry payloads may be stored uncompressed or compressed with raw Deflate (zlib, no header)

### File header
```rust
{
  u32          magicNumber         // "KVRF"
  u32          fileSize            // total byte length of the file
  u32          version             // format version number
  u8           stringHashAlgorithm // hash function used to build the hash table
  u8           reserved[3]         // zero-padding
  i32          entryCount          // total number of key/value entries
  i32          hashTableSize       // number of buckets in the on-disk hash table
  i32          reserved1[2]        // unused
  KvromAddress hashTableAddress    // file offset of the first hash table bucket
  KvromAddress reserved2[3]        // unused
}
```

Where `KvromAddress` is:
```rust
{
  u32 offset // 32-bit byte offset from the start of the file
}
```

Total header size: 48 bytes.

### Hash table
The hash table begins at `hashTableAddress` and contains `hashTableSize` buckets. Each bucket is a `KvromAddress` pointing to the first entry in a singly-linked chain, or `0x00000000` if the bucket is empty.

```rust
{
  KvromAddress bucket[0]
  KvromAddress bucket[1]
  // etc.
  KvromAddress bucket[hashTableSize-1]
}
```

### Entry structure
Each entry starts with a fixed-size header immediately followed by its payload:
```rust
{
  KvromAddress nextAddress            // next entry in the same bucket chain, or 0
  i32          keyHash                // hash of the key as computed by the writer
  u16          keyTypeAndDataCategory // packed field: bits [2:0] = key type, bits [15:3] = data category
  u8           compressionType        // 0 = uncompressed, 1 = raw Deflate
  u8           reserved               // zero-padding
  i32          dataSize               // byte length of the payload that follows
}
```

Total entry header size: 16 bytes.

The `keyTypeAndDataCategory` field packs two values:
* **Key type** (bits 2–0): how the key bytes in the payload are interpreted (see [Key types](#key-types))
* **Data category** (bits 15–3): semantic category of the string, used by the game to look up strings by category rather than by key

### Entry payload
The payload immediately follows the entry header. Its layout is the same whether the data is uncompressed or Deflate-compressed. When `compressionType` is `1`, the payload starts with a 4-byte little-endian decompressed-size hint before the raw Deflate stream:
```rust
// compressionType == 0 (uncompressed)
{
  u16 keyLength // byte length of the key field
  u8  key[]     // key bytes (encoding depends on key type)
  u8  value[]   // UTF-16-LE string, length = dataSize - 2 - keyLength
}

// compressionType == 1 (raw Deflate)
{
  u32 decompressedSize // hint for the decompressor to pre-allocate a buffer
  u8  deflateStream[]  // raw Deflate-compressed payload
  // once inflated, the decompressed bytes follow the same layout as above
}
```

### Key types
The low 3 bits of `keyTypeAndDataCategory` determine how the key bytes are decoded:
| Type | Encoding |
|------|----------|
| `0`  | UTF-16-LE string |
| `1`  | UTF-16-LE string |
| `2`  | 64-bit unsigned integer, little-endian, rendered as `0x` followed by uppercase hex digits |
| other | Raw bytes, rendered as the literal prefix `unknown` followed by uppercase hex digits |

Types 0 and 1 are treated identically by this converter.

### Duplicate keys
A key may appear more than once across hash chains, each occurrence carrying a different value. kvrom2json merges duplicates into a JSON array, and empty duplicate values (pairs where the value string is empty) are silently discarded.

## Tags in string values
String values may contain inline tags that the game engine expands at render time. They are written in square brackets with optional XML-style attributes.

Some tags may be missing from these lists, since they are taken directly from the game's texts and some may be added or removed in a game update.

### Spacing tags
* `[C:Nbsp ]` Non-breaking space
* `[C:Nbh ]` Non-breaking hyphen
* `[C:Lsb ]` Left square bracket literal
* `[C:Rsb ]` Right square bracket literal

### Text style tags
* `[Ctrl:Bold]` - `[/Ctrl:Bold]`
* `[Ctrl:Italic]` - `[/Ctrl:Italic]`
* `[Ctrl:OL]` - `[/Ctrl:OL]` Ordered list
* `[Ctrl:LI]` - `[/Ctrl:LI]` List item

### Inline image tags
* `[Img:Element id="N" ]` Inline energy-type icon referenced by slot index `N`
* `[Img:Element name="X" ]` Inline energy-type icon referenced by type letter
  * `C` Colorless
  * `D` Darkness
  * `F` Fighting
  * `G` Grass
  * `L` Lightning
  * `M` Metal
  * `P` Psychic
  * `R` Fire
  * `W` Water
* `[Img:ex ]` Inline -ex rarity icon

### Numeric insertion tags
* `[Num:Int max_length="N" ]` Insert an integer value, at most `N` digits
  * `plural_only=""` Suppresses display of `1` (used with `[Gr:Count]`)
  * `visible="0"` Hides the number (used for plural agreement only)
  * `no_delimiter="1"` Suppresses thousands separators
  * `zero_padding="N"` Left-pads the number with zeros to `N` digits
  * `id="N"` References a specific numbered slot when multiple counters are in use
  * `ref="N"` References a previously defined slot for agreement
* `[Num:Float id="N" ]` Insert a floating-point value
  * `decimal_padding="N"` Forces `N` decimal places

### Grammatical agreement tags
* `[Gr:Count s="singular" p="plural" ]` Chooses between the singular or plural form based on the number provided by the nearest `[Num:Int]`
  * `two="form"` Optional dual form used when the count is exactly 2
  * `one="form"` Optional form for exactly 1
  * `id="N"` References a specific numbered slot when multiple counters are in use
  * `ref="N"` References a previously defined slot for agreement

### Date and time insertion tags
* `[Date:DateTime format="X" ]` Insert a formatted date/time value
  * `d` Short date
  * `g` Short date and time
  * `G` Long date and time
  * `t` Short time only
  * `id="N"`
  * `max_length="N"`

### String insertion tags
* `[Str:Free id="N" ]`
* `[Str:PlayerName id="N" ]`
* `[Str:NickName]`
* `[Str:PlayerCode]`

### Dynamic text insertion tags
* `[Text:CardName v="KEY" ]`
* `[Text:CardName id="N" ]`
* `[Text:CardNameWithSubName id="N" ]`
* `[Text:AbilityName id="N" ]`
  * `suffix="Short"`
* `[Text:AttackName id="N" ]`
* `[Text:SpecialCondition id="N" ]`
  * `style="plain"`
  * `plural="1"`
* `[Text:AdditionalName v="KEY" type="X" ]`
  * `type="A"` Form suffix
  * `type="B"` Style variant
  * `type="origin"` Origin form
  * `type="region"` Regional form name
* `[Text:EvolutionPokemon id="N" ]`
  * `num_ref="N"`
  * `style="plain"`
* `[Text:Char v="KEY" ]`

### Master-data reference tags
These tags reference master-data tables rather than the localisation store.:
* `[Mst:Rarity v="KEY" ]`
* `[Mst:PackName v="KEY" ]`
* `[Mst:SubPackName]`
* `[Mst:PackDescription]`
* `[Mst:PackTableName id="N" ]`
* `[Mst:PackTextLabel]`
* `[Mst:PackPowerChargerName]`
* `[Mst:PackPowerChargerDescription]`
* `[Mst:ExpansionName]`
* `[Mst:ExpansionNameLong id="N" ]`
* `[Mst:SeriesName]`
* `[Mst:SeriesNameLong]`
* `[Mst:CardCharacterName v="KEY" ]`
* `[Mst:CommonCharacterName v="KEY" ]`
* `[Mst:CardAttackName]`
* `[Mst:CardAttackDescription]`
* `[Mst:CardAbilityName]`
* `[Mst:CardAbilityNameShort]`
* `[Mst:CardAbilityDescription]`
* `[Mst:CardFlavorText]`
* `[Mst:CardFrameName id="N" ]`
* `[Mst:CardFrameDescription]`
* `[Mst:CardSkinName id="N" ]`
* `[Mst:CardSkinDescription]`
* `[Mst:CardCharacterLibrarySpec]`
* `[Mst:CardIllustratorNameNoTranslation]`
* `[Mst:CardTotalMilestoneTips]`
* `[Mst:CardCollectionMissionsDescription]`
* `[Mst:MainRankName id="N" ]`
* `[Mst:MainAndSubRank]`
* `[Mst:RankMatchSeasonNameNoTranslation id="N" ]`
* `[Mst:RankRewardDivision]`
* `[Mst:EnergyTypeName id="N" ]`
* `[Mst:EnergyTypeNameLong id="N" ]`
  * `case="lower"`
* `[Mst:EnergyTypeNamePokemon id="N" ]`
* `[Mst:SpecialConditionType]`
* `[Mst:StageType]`
* `[Mst:StageTypePokemon]`
* `[Mst:TrainerType]`
* `[Mst:CurrencyName id="N" ]`
* `[Mst:CurrencyDescription]`
* `[Mst:ItemShopCategoryName]`
* `[Mst:ItemShopTabName]`
* `[Mst:ItemShopProductName id="N" ]`
* `[Mst:ItemExchangeSubCategory]`
* `[Mst:PokeGoldShopPokeGoldShopCategoryName]`
* `[Mst:PokeGoldShopProductName id="N" ]`
* `[Mst:PeripheralGoodsName v="KEY" ]`
* `[Mst:PeripheralGoodsShortName]`
* `[Mst:PeripheralGoodsDescription]`
* `[Mst:ProfileDecorationName v="KEY" ]`
* `[Mst:ProfileDecorationDescription]`
* `[Mst:ProfileDecorationSubDescription]`
* `[Mst:ProfileMessage]`
* `[Mst:RewardTicketName]`
* `[Mst:RewardTicketDescription]`
* `[Mst:TradeItemName]`
* `[Mst:TradeItemDescription]`
* `[Mst:TradeTicketName id="N" ]`
  * `case="lower"`
  * `plural="1"`
* `[Mst:TradePowerChargerName]`
* `[Mst:TradePowerChargerDescription]`
* `[Mst:TradeStanceMessage]`
* `[Mst:ChallengePowerChargerName]`
* `[Mst:ChallengePowerChargerDescription]`
* `[Mst:EventPowerChargerName]`
* `[Mst:EventPowerChargerDescription]`
* `[Mst:RevivalClockName]`
* `[Mst:RevivalClockDescription]`
* `[Mst:ExpNameNoTranslation]`
* `[Mst:ExpDescription]`
* `[Mst:RentalDeckName id="N" ]`
* `[Mst:RentalDeckDescription]`
* `[Mst:ThemeDeckRecipeName]`
  * `case="lower"`
* `[Mst:ThemeDeckRecipeDescription]`
* `[Mst:TutorialDeckName]`
* `[Mst:SoloBattleNPCDeckName]`
* `[Mst:SoloBattleTryMaxKey]`
* `[Mst:SoloBattleTrySumKey]`
* `[Mst:SoloEventBattleGroupName]`
* `[Mst:SoloRandomBattleGroupName]`
* `[Mst:SoloRandomBattleName]`
* `[Mst:PvpEmblemEventBattleName]`
* `[Mst:ActivityMissionsDescription]`
* `[Mst:MissionGroupsDescription]`
* `[Mst:MissionGroupsDescriptionNoBR]`
* `[Mst:MissionTabLabel]`
* `[Mst:TrophiesDescription]`
* `[Mst:LevelAccessControlDescription]`
* `[Mst:CollectionFileHashTag]`
* `[Mst:CollectionBoardHashTag]`
* `[Mst:CountryName]`
* `[Mst:PromotionName]`
* `[Mst:PromotionCardSource]`
* `[Mst:PresentBoxDescription]`
* `[Mst:FixedNotice]`
* `[Mst:MacroFormatCommon]`
* `[Mst:RightEndDisplayName]`
* `[Mst:BotNameNoTranslation]`
* `[Mst:MstTagNoTranslation]`
