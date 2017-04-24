#include <intrin.h>
#include "codec.h"

#include "tables.inl"

// Input data is expected to be padded with one extra byte of length
size_t Encode( uint8_t initialDictionary, const uint8_t* data, uint64_t dataSize, uint64_t* out )
{
    size_t readBits = 0;

    uint64_t outNibbleCount = 0;

    uint64_t* outCursor = out;

    uint32_t probability = 0;

    uint8_t dictionary = initialDictionary;

    outCursor[ 0 ] = 0;
    outCursor[ 1 ] = 0;

    while ( readBits < dataSize )
    {
        // unaligned read.
        uint8_t bits = static_cast< uint8_t >( *reinterpret_cast< const uint16_t*>( data + ( readBits >> 3 ) ) >> ( readBits & 0x7 ) );

        uint8_t dictionaryEntry = encodingTable[ 256 * dictionary + bits ];

        uint64_t code = dictionaryEntry & 0xF;

        outCursor[ ( outNibbleCount & 0xF ) > 7 ] |= 
            code << ( ( outNibbleCount & 7 ) * 8 + ( ( outNibbleCount & 0x1F ) > 0xF ) * 4 );

        readBits += dictionaryEntry >> 4; // top 4 bits of dictionary entry is bit count.

                                          // Note this weights smaller dictionary runs at a higher weight than long runs.
                                          // But we're going for fast over perfect here.
        probability += decodingTable[ 16 * 4 * dictionary + 48 + code ];

        ++outNibbleCount;

        if ( ( outNibbleCount & 0x1F ) == 0 )
        {
            outCursor += 2;

            outCursor[ 0 ] = 0;
            outCursor[ 1 ] = 0;

            // Crude approximate - may not get the absolute closest dictionary
            // But will only be one off worst case. Want to keep this cheap.
            dictionary  = ( probability >> 7 ); 
            probability = 0;
        }
    }

    if ( ( outNibbleCount & 0x1F ) != 0 )
    {
        outCursor += 2;
    }

    return outCursor - out;
}

// Note, it's assumed that the compressed buffer is padded out to 16 byte alignment with 0 nibbles.
// Out buffer is assumed to have 32 bytes extra space at the end.
void Decode( uint8_t initialDictionary, const uint64_t* compressed, uint64_t uncompressedSize, uint8_t* outBuffer )
{
    uint64_t decompressedBits = 0;
    __m128i  nibbleMask       = _mm_set1_epi8( 0xF );
    __m128i  zero             = _mm_setzero_si128();
    uint32_t dictionaryIndex  = initialDictionary; 

    uint64_t bitBuffer = 0;

#define EXTRACT_AND_OUTPUT( extractIndex )                                                                                \
    {                                                                                                                     \
        uint64_t symbolsExtract  = static_cast< uint64_t >( _mm_extract_epi64( symbols, extractIndex ) );                 \
        uint64_t maskExtract     = static_cast< uint64_t >( _mm_extract_epi64( mask, extractIndex ) );                    \
        uint64_t countExtract    = static_cast< uint64_t >( _mm_extract_epi64( sumCounts, extractIndex ) );               \
                                                                                                                          \
        uint64_t compacted  = _pext_u64( symbolsExtract, maskExtract );                                                   \
                                                                                                                          \
        bitBuffer |= ( compacted << ( decompressedBits & 7 ) );                                                           \
                                                                                                                          \
        *reinterpret_cast< uint64_t* >( outBuffer + ( decompressedBits >> 3 ) ) = bitBuffer;                              \
                                                                                                                          \
        decompressedBits += countExtract;                                                                                 \
                                                                                                                          \
        bitBuffer = ( -( ( decompressedBits & 7 ) != 0 ) ) & ( compacted >> ( countExtract - ( decompressedBits & 7 ) ) );\
    }                                                                                                                     \

    while ( decompressedBits < uncompressedSize )
    {
        __m128i dictionary     = _mm_loadu_si128( reinterpret_cast< const __m128i* >( decodingTable ) + dictionaryIndex * 4 );
        __m128i extract        = _mm_loadu_si128( reinterpret_cast< const __m128i* >( decodingTable ) + dictionaryIndex * 4 + 1 );
        __m128i counts         = _mm_loadu_si128( reinterpret_cast< const __m128i* >( decodingTable ) + dictionaryIndex * 4 + 2 );

        __m128i  nibbles       = _mm_loadu_si128( reinterpret_cast< const __m128i* >( compressed ) );
        __m128i  loNibbles     = _mm_and_si128( nibbles, nibbleMask );

        {
            __m128i  symbols   = _mm_shuffle_epi8( dictionary, loNibbles );
            __m128i  mask      = _mm_shuffle_epi8( extract, loNibbles );
            __m128i  count     = _mm_shuffle_epi8( counts, loNibbles );
            __m128i  sumCounts = _mm_sad_epu8( count, zero );

            EXTRACT_AND_OUTPUT( 0 );
            EXTRACT_AND_OUTPUT( 1 );
        }

        __m128i  hiNibbles     = _mm_and_si128( _mm_srli_epi32( nibbles, 4 ), nibbleMask );

        {
            __m128i  symbols   = _mm_shuffle_epi8( dictionary, hiNibbles );
            __m128i  mask      = _mm_shuffle_epi8( extract, hiNibbles );
            __m128i  count     = _mm_shuffle_epi8( counts, hiNibbles );
            __m128i  sumCounts = _mm_sad_epu8( count, zero );

            EXTRACT_AND_OUTPUT( 0 );
            EXTRACT_AND_OUTPUT( 1 );
        }

        __m128i probabilities = _mm_loadu_si128( reinterpret_cast< const __m128i* >( decodingTable ) + dictionaryIndex * 4 + 3 );
        __m128i loProbs       = _mm_shuffle_epi8( probabilities, loNibbles );
        __m128i sumLoProbs    = _mm_sad_epu8( loProbs, zero );
        __m128i hiProbs       = _mm_shuffle_epi8( probabilities, hiNibbles );
        __m128i sumHiProbs    = _mm_sad_epu8( hiProbs, zero );
        __m128i sumProbs      = _mm_add_epi32( sumHiProbs, sumLoProbs );

        dictionaryIndex = ( _mm_extract_epi32( sumProbs, 0 ) + _mm_extract_epi32( sumProbs, 2 ) ) >> 7;
        
        compressed += 2;
    }

    if ( ( decompressedBits & 7 ) > 0 )
    { 
        *( outBuffer + ( decompressedBits >> 3 ) ) = static_cast< uint8_t >( bitBuffer );
    }
}