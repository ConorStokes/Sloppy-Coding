// DictionaryGenerator.cpp : Defines the entry point for the console application.
//
#include <stdint.h>
#include <algorithm>
#include <intrin.h>
#include <stdio.h>

struct TableEntry
{
    double  probability;
    uint8_t bitCode;
    uint8_t bitLength;
};

static const uint32_t DICTIONARY_COUNT = 64;

int main()
{
    uint8_t decodingTable[ 16 * 4 * DICTIONARY_COUNT ];
    uint8_t encodingTable[ 256 * DICTIONARY_COUNT ];

    for ( uint32_t dictionaryIndex = 0; dictionaryIndex < DICTIONARY_COUNT; ++dictionaryIndex )
    {
        double probability;

        if ( dictionaryIndex == 0 )
        {
            probability = 1.0 / 256.0;
        }
        else if ( dictionaryIndex == DICTIONARY_COUNT - 1 )
        {
            probability = 1.0 - ( 1.0 / 256.0 );
        }
        else
        {
            probability = double( dictionaryIndex ) / double( DICTIONARY_COUNT - 1 );
        }

        TableEntry entries[ 16 ] = {};

        entries[ 0 ] = { 1.0 - probability, 0, 1 };
        entries[ 1 ] = { probability, 1, 1 };

        auto comparison = []( const TableEntry& left, const TableEntry& right ) { return left.probability < right.probability ; };

        std::make_heap( entries, entries + 2, comparison );

        for ( uint32_t dictionaryEntry = 1; dictionaryEntry < 15; ++dictionaryEntry )
        {
            TableEntry* top = entries;

            uint8_t newLength = top->bitLength + 1;

            double lengthProbFactor = ( newLength < 8 );

            TableEntry new0 = { top->probability * ( 1.0 - probability ) * lengthProbFactor, top->bitCode, newLength };
            TableEntry new1 = { top->probability * probability * lengthProbFactor, static_cast< uint8_t >( top->bitCode | ( 1 << top->bitLength ) ), newLength };

            std::pop_heap( entries, entries + dictionaryEntry + 1, comparison );

            entries[ dictionaryEntry ] = new0;
            entries[ dictionaryEntry + 1 ] = new1;

            std::push_heap( entries, entries + dictionaryEntry + 1, comparison );
            std::push_heap( entries, entries + dictionaryEntry + 2, comparison );
        }

        uint8_t* symbols       = decodingTable + dictionaryIndex * 16 * 4;
        uint8_t* masks         = decodingTable + dictionaryIndex * 16 * 4 + 16;
        uint8_t* counts        = decodingTable + dictionaryIndex * 16 * 4 + 32;
        uint8_t* probabilities = decodingTable + dictionaryIndex * 16 * 4 + 48;
        uint8_t* encoding      = encodingTable + 256 * dictionaryIndex;

        for ( uint32_t dictionaryEntry = 0; dictionaryEntry < 16; ++dictionaryEntry )
        {
            TableEntry* entry = entries + dictionaryEntry;

            symbols[ dictionaryEntry ] = entry->bitCode;
            counts[ dictionaryEntry ]  = entry->bitLength;
            masks[ dictionaryEntry ]   = ( 1 << entry->bitLength ) - 1;

            probabilities[ dictionaryEntry ] = static_cast< uint8_t >( 255.0 * double( __popcnt( entry->bitCode ) ) / double( entry->bitLength ) );

            uint32_t encodingEntries = 1 << ( 8 - entry->bitLength );

            for ( uint32_t encodingEntry = 0; encodingEntry < encodingEntries; ++encodingEntry )
            {
                encoding[ uint32_t( entry->bitCode ) | ( encodingEntry << entry->bitLength ) ] = 
                    static_cast< uint8_t >( dictionaryEntry | uint32_t( entry->bitLength << 4 ) );
            }
        }
    }

    printf( "static const uint8_t decodingTable[ 64 * 4 * 16 ] = \n{\n" );
    
    for ( uint32_t decodingRow = 0; decodingRow < 64 * 4; ++decodingRow )
    {
        uint8_t* row = decodingTable + ( decodingRow * 16 );

        printf( "    " );

        for ( uint32_t column = 0; column < 16; ++column )
        {
            printf( "%d", int32_t( row[ column ] ) );
            
            if ( decodingRow != ( 64 * 16 ) - 1 || column != 15 )
            {
                printf( "," );
            }
        }

        printf( "\n" );
    }

    printf( "};\n\n\n" );

    printf( "static const uint8_t encodingTable[ 64 * 256 ] = \n{\n" );

    for ( uint32_t encodingRow = 0; encodingRow < ( 64 * 256 ) / 16; ++encodingRow )
    {
        uint8_t* row = encodingTable + ( encodingRow * 16 );

        printf( "    " );

        for ( uint32_t column = 0; column < 16; ++column )
        {
            printf( "%d", int32_t( row[ column ] ) );

            if ( encodingRow != ( ( 64 * 256 ) / 16 ) - 1 || column != 15 )
            {
                printf( "," );
            }
        }

        printf( "\n" );
    }

    printf( "};\n\n\n" );

    return 0;
}

