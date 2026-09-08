#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Proper .ts file translator that works message-by-message with full XML fidelity.
Preserves multiline strings, format variables, HTML tags, and all structure.

The file is parsed with ElementTree to decide what needs translating, but the
translations are spliced back into the raw file text rather than written out from
the tree.  ElementTree's writer cannot reproduce a Qt .ts file: it discards the
<!DOCTYPE TS> declaration and every XML comment, and re-indents the whole
document in its own style.  Writing through it previously stripped the DOCTYPE
from all 31 catalogues and would have destroyed the translator attribution
comment at the top of Ukrainska.uk.ts, and it turned any change, however small,
into a whole-file diff.  Splicing leaves every other byte exactly as lupdate
wrote it.
"""
import re
import xml.etree.ElementTree as ET
from anthropic import Anthropic
import sys


def get_all_text(element):
    """Get text from an element (not including child element text)"""
    if element.text is None:
        return ""
    return element.text


def extract_unfinished_messages(ts_file):
    """Extract all messages with empty translations from the .ts file.

    Returns (messages, total).  Each message records its 'index' -- its position
    in document order among *all* <message> elements -- which is how the splice
    step locates it again in the raw text.  'total' is that element count, used
    to prove the text walk and the parsed tree agree before anything is written.
    """
    tree = ET.parse(ts_file)
    root = tree.getroot()

    all_messages = root.findall('.//message')
    messages = []
    for index, message in enumerate(all_messages):
        # Skip plural forms (numerus="yes") - they need special handling
        if message.get('numerus') == 'yes':
            continue

        translation_elem = message.find('translation')
        source_elem = message.find('source')

        if translation_elem is None or source_elem is None:
            continue

        source_text = get_all_text(source_elem)
        if not source_text or not source_text.strip():
            continue

        # variants="yes" is on <translation>, not <message>
        is_variant = translation_elem.get('variants') == 'yes'

        if is_variant:
            # For variant messages, check if any <lengthvariant> has content
            lengthvariants = translation_elem.findall('lengthvariant')
            any_translated = any(lv.text and lv.text.strip() for lv in lengthvariants)
            if any_translated:
                continue  # Already translated, skip

            msg_data = {
                'source': source_text,
                'index': index,
                'is_variant': True,
                'num_variants': len(lengthvariants),
            }
        else:
            translation_text = get_all_text(translation_elem)
            if translation_text.strip():
                continue  # Already translated, skip

            msg_data = {
                'source': source_text,
                'index': index,
                'is_variant': False,
            }

        messages.append(msg_data)

    return messages, len(all_messages)


def translate_messages_batch(messages_batch, client, target_language="English"):
    """Translate a batch of messages using Claude"""
    if not messages_batch:
        return {}

    prompt = f"""Translate the following English strings to {target_language}.

IMPORTANT RULES:
- Keep format variables (%1, %2, %3, etc.) exactly as they are
- Keep HTML tags (<b>, </b>, <i>, </i>, etc.) exactly as they are
- Keep all newlines and whitespace exactly as they are
- Preserve any leading or trailing spaces in the source string exactly
- Device names and abbreviations (CPAP, BiPAP, ResMed, etc.) stay in English
- Return ONLY the translations, one per line, numbered to match

Format:
1. [translation]
2. [translation]
... etc

Sources to translate:
"""

    for i, msg in enumerate(messages_batch, 1):
        display_source = msg['source'].replace('\n', '\\n')
        prompt += f"\n{i}. {display_source}"

    prompt += "\n\nProvide translations:"

    try:
        response = client.messages.create(
            model="claude-sonnet-5",
            max_tokens=8192,
            # Sonnet 5 runs adaptive thinking unless told otherwise, which put a
            # ThinkingBlock first in response.content and spent part of the token
            # budget reasoning about what is a mechanical substitution.
            thinking={"type": "disabled"},
            messages=[{"role": "user", "content": prompt}]
        )

        # response.content is a list of blocks (TextBlock, ThinkingBlock, ...);
        # take the text ones rather than assuming the first block is text.
        response_text = "".join(block.text for block in response.content
                                if block.type == "text")

        if not response_text:
            # A refusal or an empty turn would otherwise look like a parse failure.
            detail = getattr(response, 'stop_details', None)
            print(f"No text in response (stop_reason={response.stop_reason}"
                  f"{', ' + str(detail) if detail else ''})")
            return {}
        if response.stop_reason == "max_tokens":
            print(f"Response hit max_tokens; batch truncated. ", end="")

        translations = {}

        for line in response_text.strip().split('\n'):
            line = line.rstrip('\r\n')  # Strip only newline chars; preserve spaces
            if not line or not line[0].isdigit():
                continue
            dot_idx = line.find('.')
            if dot_idx > 0:
                try:
                    num = int(line[:dot_idx])
                    rest = line[dot_idx + 1:]
                    # Remove exactly the one separator space after "N. "; preserve the rest
                    trans = rest[1:] if rest.startswith(' ') else rest
                    if trans:
                        translations[num] = trans
                except ValueError:
                    pass

        return translations

    except Exception as e:
        print(f"Error translating batch: {e}")
        return {}


# "&" has to be substituted first or it would corrupt the entities that follow.
# Qt's own tools escape all five of these in element text; matching them keeps a
# later lupdate run from rewriting the lines this script touched.
XML_ESCAPES = (("&", "&amp;"), ("<", "&lt;"), (">", "&gt;"),
               ('"', "&quot;"), ("'", "&apos;"))

MESSAGE_RE = re.compile(r'<message\b[^>]*>.*?</message>', re.S)
TRANSLATION_RE = re.compile(r'(<translation\b[^>]*>)(.*?)(</translation>)', re.S)
LENGTHVARIANT_RE = re.compile(r'(<lengthvariant\b[^>]*>)(.*?)(</lengthvariant>)', re.S)


def xml_escape(text):
    """Escape text for an XML element body, the way Qt's own tools do."""
    for raw, entity in XML_ESCAPES:
        text = text.replace(raw, entity)
    return text


def apply_translations(text, resolved, total_messages, newline='\n'):
    """Splice translations into the raw file text; return (new_text, count).

    *resolved* maps a message's document-order index to (is_variant, translation).
    Only the body of the <translation> element -- or of its first <lengthvariant>
    -- is replaced, so attributes, indentation, comments and the DOCTYPE survive
    untouched.  type="unfinished" is left in place so Linguist still flags these
    as machine output needing review.

    Edits are applied back to front so that the offsets of the earlier ones stay
    valid.  If the number of <message> blocks found in the text does not match the
    number in the parsed tree the two views have diverged, and rather than write
    to a guessed offset the function raises.
    """
    blocks = list(MESSAGE_RE.finditer(text))
    if len(blocks) != total_messages:
        raise RuntimeError(
            "found %d <message> blocks in the file text but %d in the parsed "
            "tree; refusing to edit" % (len(blocks), total_messages))

    edits = []
    for index, (is_variant, translation) in resolved.items():
        block = blocks[index]
        body = block.group(0)
        # A variant translation holds its text in <lengthvariant> children; the
        # <translation> element's own text is whitespace and must stay that way.
        target = (LENGTHVARIANT_RE if is_variant else TRANSLATION_RE).search(body)
        if target is None:
            print("  warning: no place to put translation for message %d" % index)
            continue

        replacement = xml_escape(translation.replace('\\n', '\n'))
        if newline != '\n':
            replacement = replacement.replace('\n', newline)
        edits.append((block.start() + target.start(2),
                      block.start() + target.end(2), replacement))

    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    return text, len(edits)


def main():
    language_map = {
        'Espaniol.es_MX': 'Spanish (Mexican)',
        'Espaniol.es': 'Spanish (Spain)',
        'Nederlands.nl': 'Dutch',
        'Deutsch.de': 'German',
        'Francais.fr': 'French',
        'Italiano.it': 'Italian',
        'Portugues.pt_BR': 'Portuguese (Brazilian)',
        'Portugues.pt': 'Portuguese',
        'Japanese.ja': 'Japanese',
        'Chinese.zh_CN': 'Chinese (Simplified)',
        'Chinese.zh_TW': 'Chinese (Traditional)',
        'Korean.ko': 'Korean',
        'Russian.ru': 'Russian',
        'Russkiy.ru': 'Russian',
        'Turkish.tr': 'Turkish',
        'Ukrainska.uk': 'Ukrainian',
        'Arabic.ar': 'Arabic',
        'Hebrew.he': 'Hebrew',
        'Greek.el': 'Greek',
        'Polski.pl': 'Polish',
        'Czech.cz': 'Czech',
        'Hungarian.hu': 'Hungarian',
        'Magyar.hu': 'Hungarian',
        'Romanian.ro': 'Romanian',
        'Bulgarian.bg': 'Bulgarian',
        'Serbian.sr': 'Serbian',
        'Croatian.hr': 'Croatian',
        'Swedish.sv': 'Swedish',
        'Svenska.sv': 'Swedish',
        'Norwegian.no': 'Norwegian',
        'Norsk.no': 'Norwegian',
        'Dansk.da': 'Danish',
        'Suomi.fi': 'Finnish',
        'Afrikaans.af': 'Afrikaans',
        'Filipino.fil': 'Filipino',
        'Thai.th': 'Thai',
    }

    if len(sys.argv) > 1:
        lang = sys.argv[1]
        ts_file = rf'C:\OSCAR\OSCAR-code\Translations\{lang}.ts'
        target_language = language_map.get(lang, lang)
    else:
        lang = 'Espaniol.es_MX'
        ts_file = r'C:\OSCAR\OSCAR-code\Translations\Espaniol.es_MX.ts'
        target_language = 'Spanish (Mexican)'

    client = Anthropic()

    print(f"Loading {lang}.ts...")
    messages, total_messages = extract_unfinished_messages(ts_file)

    variants = [m for m in messages if m.get('is_variant')]
    regular = [m for m in messages if not m.get('is_variant')]
    print(f"Found {len(messages)} messages to translate ({len(regular)} regular, {len(variants)} variants)")

    if not messages:
        print("Nothing to translate.")
        return

    batch_size = 20
    # Collected across every batch, then spliced into the file in one pass at the
    # end: {document index -> (is_variant, translation)}.
    resolved = {}

    for batch_start in range(0, len(messages), batch_size):
        batch = messages[batch_start:batch_start + batch_size]
        batch_end = batch_start + len(batch)

        print(f"Processing {batch_start + 1}-{batch_end}...", end=" ", flush=True)

        translations = translate_messages_batch(batch, client, target_language)

        if translations:
            applied = 0
            for position, msg_data in enumerate(batch, 1):
                if position in translations:
                    resolved[msg_data['index']] = (msg_data.get('is_variant', False),
                                                   translations[position])
                    applied += 1
            print(f"{applied}/{len(batch)}")
        else:
            print("Failed")

    print(f"\n{'='*60}")
    print(f"Total translated: {len(resolved)}/{len(messages)}")

    if resolved:
        print("Writing .ts file...")
        # Read and write as bytes so the file's own line endings are preserved.
        text = open(ts_file, 'rb').read().decode('utf-8')
        newline = '\r\n' if '\r\n' in text else '\n'
        text, spliced = apply_translations(text, resolved, total_messages, newline)
        open(ts_file, 'wb').write(text.encode('utf-8'))
        print(f"Done: {ts_file} ({spliced} spliced)")
    else:
        print("No translations applied.")


if __name__ == '__main__':
    main()
