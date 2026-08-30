#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Proper .ts file translator that works message-by-message with full XML fidelity.
Preserves multiline strings, format variables, HTML tags, and all structure.
"""
import xml.etree.ElementTree as ET
from anthropic import Anthropic
import sys


def get_all_text(element):
    """Get text from an element (not including child element text)"""
    if element.text is None:
        return ""
    return element.text


def extract_unfinished_messages(ts_file):
    """Extract all messages with empty translations from the .ts file"""
    tree = ET.parse(ts_file)
    root = tree.getroot()

    messages = []
    for message in root.findall('.//message'):
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
                'element': translation_elem,
                'message_elem': message,
                'is_variant': True,
                'num_variants': len(lengthvariants),
            }
        else:
            translation_text = get_all_text(translation_elem)
            if translation_text.strip():
                continue  # Already translated, skip

            msg_data = {
                'source': source_text,
                'element': translation_elem,
                'message_elem': message,
                'is_variant': False,
            }

        messages.append(msg_data)

    return messages, root


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
            model="claude-sonnet-4-6",
            max_tokens=4096,
            messages=[{"role": "user", "content": prompt}]
        )

        response_text = response.content[0].text
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


def apply_translations(messages, translations):
    """Apply translations back to the XML elements"""
    applied = 0

    for i, msg_data in enumerate(messages, 1):
        if i not in translations:
            continue

        trans_text = translations[i].replace('\\n', '\n')
        trans_elem = msg_data['element']

        if msg_data.get('is_variant'):
            # Write translation into the first <lengthvariant> only.
            # Do NOT touch trans_elem.text — it must stay as whitespace.
            # Leave other lengthvariants empty (Qt accepts this).
            lengthvariants = trans_elem.findall('lengthvariant')
            if lengthvariants:
                lengthvariants[0].text = trans_text
            else:
                # No lengthvariant children yet — create one
                lv = ET.SubElement(trans_elem, 'lengthvariant')
                lv.text = trans_text
        else:
            # Regular message: set translation text directly
            trans_elem.text = trans_text

        # Preserve type="unfinished" so linguist can identify auto-translated strings
        applied += 1

    return applied


def indent_xml(elem, level=0):
    """Add proper indentation to XML elements"""
    indent = "\n" + ("  " * level)
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = indent + "  "
        if not elem.tail or not elem.tail.strip():
            elem.tail = indent
        for child in elem:
            indent_xml(child, level + 1)
        if not child.tail or not child.tail.strip():
            child.tail = indent
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = indent


def write_xml(root, output_file):
    """Write XML with proper formatting"""
    tree = ET.ElementTree(root)
    tree.write(output_file, encoding='utf-8', xml_declaration=True)


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
    messages, root = extract_unfinished_messages(ts_file)

    variants = [m for m in messages if m.get('is_variant')]
    regular = [m for m in messages if not m.get('is_variant')]
    print(f"Found {len(messages)} messages to translate ({len(regular)} regular, {len(variants)} variants)")

    if not messages:
        print("Nothing to translate.")
        return

    batch_size = 20
    total_applied = 0

    for batch_start in range(0, len(messages), batch_size):
        batch = messages[batch_start:batch_start + batch_size]
        batch_end = batch_start + len(batch)

        print(f"Processing {batch_start + 1}-{batch_end}...", end=" ", flush=True)

        translations = translate_messages_batch(batch, client, target_language)

        if translations:
            applied = apply_translations(batch, translations)
            total_applied += applied
            print(f"{applied}/{len(batch)}")
        else:
            print("Failed")

    print(f"\n{'='*60}")
    print(f"Total translated: {total_applied}/{len(messages)}")

    if total_applied > 0:
        print("Writing .ts file...")
        indent_xml(root)
        write_xml(root, ts_file)
        print(f"Done: {ts_file}")
    else:
        print("No translations applied.")


if __name__ == '__main__':
    main()
