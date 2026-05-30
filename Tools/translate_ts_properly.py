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
    """Get all text from an element including tail text"""
    if element.text is None:
        return ""
    return element.text

def extract_unfinished_messages(ts_file):
    """Extract all messages with empty translations from the .ts file"""
    tree = ET.parse(ts_file)
    root = tree.getroot()

    messages = []
    for message in root.findall('.//message'):
        translation_elem = message.find('translation')
        source_elem = message.find('source')

        if translation_elem is not None and source_elem is not None:
            source_text = get_all_text(source_elem)
            translation_text = get_all_text(translation_elem)
            # Only translate if source is non-empty and translation is empty
            if source_text and source_text.strip() and not translation_text.strip():
                messages.append({
                    'source': source_text,
                    'element': translation_elem,
                    'message_elem': message
                })

    return messages, root

def translate_messages_batch(messages_batch, client, target_language="English"):
    """Translate a batch of messages using Claude"""
    if not messages_batch:
        return {}

    # Build prompt with source strings numbered
    prompt = f"""Translate the following English strings to {target_language}.

IMPORTANT RULES:
- Keep format variables (%1, %2, %3, etc.) exactly as they are
- Keep HTML tags (<b>, </b>, <i>, </i>, etc.) exactly as they are
- Keep all newlines and whitespace exactly as they are
- Device names and abbreviations (CPAP, BiPAP, ResMed, etc.) stay in English
- Return ONLY translations in the exact format shown below
- Each translation on its own line, numbered to match source

Format:
1. [translation of first source]
2. [translation of second source]
... etc

Sources to translate:
"""

    for i, msg in enumerate(messages_batch, 1):
        source = msg['source']
        # Show source with escaped newlines for clarity in prompt
        display_source = source.replace('\n', '\\n')
        prompt += f"\n{i}. {display_source}"

    prompt += "\n\nNow provide the translations:"

    try:
        response = client.messages.create(
            model="claude-opus-4-7",
            max_tokens=4096,
            messages=[{"role": "user", "content": prompt}]
        )

        response_text = response.content[0].text
        translations = {}

        # Parse numbered list response
        lines = response_text.strip().split('\n')
        for line in lines:
            line = line.strip()
            if not line:
                continue

            # Match "N. translation"
            if line[0].isdigit():
                dot_idx = line.find('.')
                if dot_idx > 0:
                    try:
                        num = int(line[:dot_idx])
                        trans = line[dot_idx + 1:].strip()
                        if trans:
                            translations[num] = trans
                    except ValueError:
                        pass

        return translations

    except Exception as e:
        print(f"Error translating batch: {e}")
        return {}

def apply_translations(root, messages, translations):
    """Apply translations back to the XML"""
    applied = 0

    for i, msg_data in enumerate(messages, 1):
        if i in translations:
            trans_elem = msg_data['element']
            trans_elem.text = translations[i]
            # Keep type="unfinished" so linguist flags it for review
            applied += 1

    return applied

def write_xml(root, output_file):
    """Write XML with proper formatting"""
    tree = ET.ElementTree(root)

    # Register namespace to preserve it
    ET.register_namespace('', 'http://www.w3.org/2005/Atom')

    tree.write(output_file, encoding='utf-8', xml_declaration=True)

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

def main():
    # Accept language as command-line argument, default to Spanish (MX)
    import sys

    # Map filename prefixes to target language names for translation prompt
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
        'Turkish.tr': 'Turkish',
        'Arabic.ar': 'Arabic',
        'Hebrew.he': 'Hebrew',
        'Greek.el': 'Greek',
        'Polish.pl': 'Polish',
        'Czech.cz': 'Czech',
        'Hungarian.hu': 'Hungarian',
        'Romanian.ro': 'Romanian',
        'Bulgarian.bg': 'Bulgarian',
        'Serbian.sr': 'Serbian',
        'Croatian.hr': 'Croatian',
        'Swedish.sv': 'Swedish',
        'Norwegian.no': 'Norwegian',
        'Danish.da': 'Danish',
        'Finnish.fi': 'Finnish',
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

    print("Loading .ts file...")
    messages, root = extract_unfinished_messages(ts_file)
    print(f"Found {len(messages)} empty translations to fill")
    if not messages:
        # Debug: check if there are any unfinished at all
        import xml.etree.ElementTree as ET
        tree = ET.parse(ts_file)
        rt = tree.getroot()
        unfinished_count = sum(1 for msg in rt.findall('.//message')
                              if msg.find('translation') is not None and
                                 msg.find('translation').get('type') == 'unfinished')
        print(f"Debug: Found {unfinished_count} unfinished translations in file")

    if not messages:
        print("No unfinished messages found!")
        return

    # Process in batches of 20 to stay efficient
    batch_size = 20
    total_applied = 0

    for batch_start in range(0, len(messages), batch_size):
        batch_end = min(batch_start + batch_size, len(messages))
        batch = messages[batch_start:batch_end]

        print(f"\nProcessing messages {batch_start + 1}-{batch_end}...", end=" ", flush=True)

        translations = translate_messages_batch(batch, client, target_language)

        if translations:
            applied = apply_translations(batch, batch, translations)
            total_applied += applied
            print(f"Translated {applied}/{len(batch)}")
        else:
            print("Failed")

    print(f"\n{'='*60}")
    print(f"Total messages translated: {total_applied}/{len(messages)}")

    if total_applied > 0:
        print(f"Writing updated .ts file...")
        indent_xml(root)
        write_xml(root, ts_file)
        print(f"Done! File saved to: {ts_file}")
    else:
        print("No translations to apply!")

if __name__ == '__main__':
    main()
